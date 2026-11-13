#include "areca_engine.h"

#include "notification/dynamic_island_announcement.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <iterator>
#include <utility>

#include "fcitx_notifications_compat.h"
#include "program_compatibility.h"
#include <fcitx-utils/dbus/bus.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/misc.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/userinterfacemanager.h>

#include "mouse_click_tracker.h"
#include "window_focus_tracker.h"

FCITX_ADDON_DECLARE_FUNCTION(DBusModule, bus, fcitx::dbus::Bus *());

namespace areca {

ArecaEngine::ArecaEngine(fcitx::Instance *instance)
    : instance_(instance), rewriteStateFactory_([this](fcitx::InputContext &) {
        return new RewriteInputState(
            config_.bambooInputMethod.value(),
            config_.spellcheckMode.value() != SpellcheckMode::Off,
            config_.spellcheckMode.value() == SpellcheckMode::Realtime,
            config_.modernStyle.value(), config_.outputCharset.value(),
            config_.enableMacro.value(), config_.capitalizeMacro.value(),
            macroRevision_, macroDefinitions(),
            config_.revertMacroOnBackspace.value());
      }),
      preeditStateFactory_([this](fcitx::InputContext &) {
        return new PreeditInputState(
            config_.bambooInputMethod.value(),
            config_.spellcheckMode.value() != SpellcheckMode::Off,
            config_.spellcheckMode.value() == SpellcheckMode::Realtime,
            config_.modernStyle.value(), config_.outputCharset.value(),
            config_.enableMacro.value(), config_.capitalizeMacro.value(),
            macroRevision_, macroDefinitions(),
            config_.revertMacroOnBackspace.value());
      }),
      adaptiveWaitMonitor_(instance_->eventLoop(), adaptiveWait_,
                           [this]() { return debugEnabled(); }),
      surroundingBackend_(
          instance_->eventLoop(), [this]() { return debugEnabled(); },
          [this](fcitx::InputContext &inputContext) {
            handleSurroundingProbeSucceeded(inputContext);
          },
          [this](fcitx::InputContext &inputContext) {
            handleSurroundingProbeFailed(inputContext);
          },
          [this](fcitx::InputContext &inputContext) -> RewriteBackend * {
            return resolveSurroundingFallbackBackend(inputContext);
          },
          &adaptiveWait_),
      surroundingV2Backend_(
          instance_->eventLoop(), [this]() { return debugEnabled(); },
          [this](fcitx::InputContext &inputContext) {
            handleSurroundingProbeSucceeded(inputContext);
          },
          [this](fcitx::InputContext &inputContext) {
            handleSurroundingProbeFailed(inputContext);
          },
          [this](fcitx::InputContext &inputContext) -> RewriteBackend * {
            return resolveSurroundingFallbackBackend(inputContext);
          },
          &adaptiveWait_),
      forwardBackspaceBackend_(instance_->eventLoop(), adaptiveWait_,
                               [this]() { return debugEnabled(); }),
      uinputDevice_([this]() { return debugEnabled(); },
                    [this]() { notifyUinputUnavailable(); }),
      uinputBackspaceBackend_(instance_->eventLoop(), uinputDevice_,
                              adaptiveWait_,
                              [this]() { return debugEnabled(); }),
      uinputShiftSelectBackend_(instance_->eventLoop(), uinputDevice_,
                                adaptiveWait_,
                                [this]() { return debugEnabled(); }),
      nativeDevice_(
          instance_->eventLoop(), [this]() { return debugEnabled(); },
          [this]() -> fcitx::dbus::Bus * {
            auto *addon = instance_->addonManager().addon("dbus", true);
            return addon ? addon->call<fcitx::IDBusModule::bus>() : nullptr;
          }),
      nativeBackspaceBackend_(instance_->eventLoop(), nativeDevice_,
                              adaptiveWait_,
                              [this]() { return debugEnabled(); }),
      nativeShiftSelectBackend_(instance_->eventLoop(), nativeDevice_,
                                adaptiveWait_,
                                [this]() { return debugEnabled(); }),
      virtualKeyboardDevice_(instance_->eventLoop(),
                             [this]() { return debugEnabled(); }),
      virtualKeyboardBackspaceBackend_(instance_->eventLoop(),
                                       virtualKeyboardDevice_,
                                       [this]() { return debugEnabled(); }),
      backendSelectionPost_(instance_->eventLoop()),
      scheduler_(
          instance_->eventLoop(),
          [this](fcitx::InputContext &inputContext) -> VietnameseEngine * {
            auto *state = inputContext.propertyFor(&rewriteStateFactory_);
            return state ? state->engine.get() : nullptr;
          },
          [this](fcitx::InputContext &inputContext) {
            return timing(inputContext);
          },
          [this]() { return debugEnabled(); },
          [this](fcitx::InputContext &inputContext,
                 const BambooResult &result) -> RewriteBackendSelection {
            auto selection = selectRewriteBackend(inputContext, result);
            noteRewriteBackendChoice(inputContext, selection.backend);
            return selection;
          }),
      rewriteHandler_(
          instance_->eventLoop(), rewriteStateFactory_, scheduler_,
          [this]() { return config_.autoCapitalizeAfterPunctuation.value(); },
          [this]() { return debugEnabled(); },
          [this](fcitx::InputContext &inputContext, const char *reason) {
            protectBackendVerdict(inputContext, reason);
          },
          [this]() { return backspaceRecoveryEnabled(); },
          [this](fcitx::InputContext &inputContext) {
            if (!advancedConfig_.forwardFirstCharacter.value()) {
              return false;
            }
            auto *state = inputContext.propertyFor(&rewriteStateFactory_);
            return inputTypeDetector_.isBrowser(
                resolveProgram(inputContext, state));
          },
          [this](fcitx::InputContext &inputContext) {
            if (!config_.restoreSurroundingText.value()) {
              return false;
            }
            // TODO: Chromium đôi khi giữ getText()/SurroundingText cũ sau khi
            // người dùng Backspace sạch address bar. Tạm tắt restore tại đây
            // cho đến khi có shadow snapshot đồng bộ được từng lần xóa. Chỉ
            // tin CapabilityFlag::Url cho quyết định này; nhận diện Omnibox
            // bằng AT-SPI có thể lẹm sang input khác khi focus chuyển nhanh.
            if (inputContext.capabilityFlags().test(
                    fcitx::CapabilityFlag::Url)) {
              return false;
            }
            // App không xoá được bằng surrounding thì snapshot của nó cũng
            // không đáng tin (vd. anvildroid-gui sau Ctrl+A rồi Backspace vẫn
            // báo đuôi "đư" của từ cũ, restore nạp nhầm vào ô đã trống).
            if (!surroundingBackendInUse(inputContext)) {
              if (debugEnabled()) {
                FCITX_INFO() << "areca: surrounding restore disabled"
                             << " reason=non-surrounding-backend"
                             << " program=" << inputContext.program();
              }
              return false;
            }
            return true;
          },
          [this](const fcitx::Key &key) {
            return isMacroBypassSpace(key, config_.macroBypassModifier.value());
          }),
      preeditHandler_(
          preeditStateFactory_, [this]() { return debugEnabled(); },
          [this]() { return config_.autoCapitalizeAfterPunctuation.value(); },
          [this]() { return backspaceRecoveryEnabled(); },
          // Preedit không restore từ surrounding: restore phải xoá từ cũ khỏi
          // ứng dụng theo snapshot, snapshot sai là xoá nhầm chữ. Tắt hẳn,
          // không theo RestoreSurroundingText.
          [] { return false; },
          [this](const fcitx::Key &key) {
            return isMacroBypassSpace(key, config_.macroBypassModifier.value());
          },
          [this](fcitx::InputContext &inputContext, uint32_t backspaceCount,
                 PreeditModeHandler::FinalizedWordEraseDone onDone) {
            return eraseFinalizedPreeditWord(inputContext, backspaceCount,
                                             std::move(onDone));
          }) {
  instance_->inputContextManager().registerProperty("arecaRewriteState",
                                                    &rewriteStateFactory_);
  instance_->inputContextManager().registerProperty("arecaPreeditState",
                                                    &preeditStateFactory_);
  inputMethodAnnouncement_ = std::make_unique<DynamicIslandAnnouncement>(
      *instance_, InputMethodIdentity{"areca", "Bộ gõ ArecaIME",
                                      "org.fcitx.Fcitx5.fcitx-areca"});
  scheduler_.setCommitObserver([this](fcitx::InputContext &inputContext,
                                      uint32_t deleteCount,
                                      const std::string &text) {
    rewriteHandler_.noteCommitted(inputContext, deleteCount, text);
  });
  surroundingTextWatcher_ = instance_->watchEvent(
      fcitx::EventType::InputContextSurroundingTextUpdated,
      fcitx::EventWatcherPhase::PostInputMethod, [this](fcitx::Event &event) {
        auto &contextEvent = static_cast<fcitx::InputContextEvent &>(event);
        auto *inputContext = contextEvent.inputContext();
        if (!inputContext) {
          return;
        }
        // Nếu backend đang ở giai đoạn probe, thông báo để nó xác nhận thành
        // công và hủy deadline timer probe.
        if (surroundingBackend_.isProbing()) {
          surroundingBackend_.notifySurroundingTextUpdated(*inputContext);
        }
        if (surroundingV2Backend_.isProbing()) {
          surroundingV2Backend_.notifySurroundingTextUpdated(*inputContext);
        }
        // Callback chỉ ghi nhận rằng frontend đã đổi text hoặc cursor. Việc đọc
        // và phục hồi được hoãn tới key press để không sửa Bamboo trong event
        // lifecycle và để nhận snapshot surrounding mới nhất.
        rewriteHandler_.onSurroundingTextUpdated(*inputContext);
        if (auto *state = preeditHandler_.stateFor(*inputContext)) {
          state->surroundingRestoreArmed = true;
        }
      });
  config_.bambooInputMethod.annotation().setList(
      BambooEngineAdapter::inputMethodNames());
  config_.outputCharset.annotation().setList(
      BambooEngineAdapter::charsetNames());
  reloadConfig();
  focusTracker_ = std::make_unique<WindowFocusTracker>(debugEnabled());
  // Keep AT-SPI monitoring alive in the background. The worker reconnects
  // when the accessibility bus appears, so fallback data is cached before a
  // Chromium frontend without CapabilityFlag::Url needs it.
  focusTracker_->start();
  inputTypeDetector_.setFocusTracker(focusTracker_.get());

  initActions();
}

ArecaEngine::~ArecaEngine() {
  inputTypeDetector_.setFocusTracker(nullptr);
  if (focusTracker_) {
    focusTracker_->stop();
  }
}

bool ArecaEngine::eraseFinalizedPreeditWord(
    fcitx::InputContext &inputContext, uint32_t backspaceCount,
    PreeditModeHandler::FinalizedWordEraseDone onDone) {
  const std::string program = resolveProgram(inputContext, nullptr);
  const bool preferHardware =
      inputTypeDetector_.isTerminal(program, inputContext.frontend()) ||
      !inputTypeDetector_.canForwardBackspaces(inputContext, program);
  auto *backend = resolvePreferredBackspaceBackend(preferHardware);
  if (!backend) {
    return false;
  }

  const auto schedulerTiming = timing(inputContext);
  RewritePlan plan;
  plan.transactionId = nextPreeditRecoveryTransactionId_++;
  if (!plan.transactionId) {
    plan.transactionId = nextPreeditRecoveryTransactionId_++;
  }
  plan.backspaceCount = backspaceCount;
  plan.backspaceDelayMs = schedulerTiming.backspaceDelayMs;
  plan.uinputBackspaceAckTimeoutMs =
      schedulerTiming.uinputBackspaceAckTimeoutMs;
  plan.afterBackspaceWaitMs = schedulerTiming.afterBackspaceWaitMs;
  plan.waylandAfterBackspaceWaitMs =
      schedulerTiming.waylandAfterBackspaceWaitMs;
  plan.ximAfterBackspaceWaitMs = schedulerTiming.ximAfterBackspaceWaitMs;
  plan.fcitx4AfterBackspaceWaitMs = schedulerTiming.fcitx4AfterBackspaceWaitMs;
  plan.dbusAfterBackspaceWaitMs = schedulerTiming.dbusAfterBackspaceWaitMs;
  plan.ibusAfterBackspaceWaitMs = schedulerTiming.ibusAfterBackspaceWaitMs;
  plan.xtestBackspaceDelayMs = schedulerTiming.xtestBackspaceDelayMs;
  plan.afterXTestBackspaceWaitMs = schedulerTiming.afterXTestBackspaceWaitMs;
  plan.waylandAfterXTestBackspaceWaitMs =
      schedulerTiming.waylandAfterXTestBackspaceWaitMs;
  plan.ximAfterXTestBackspaceWaitMs =
      schedulerTiming.ximAfterXTestBackspaceWaitMs;
  plan.fcitx4AfterXTestBackspaceWaitMs =
      schedulerTiming.fcitx4AfterXTestBackspaceWaitMs;
  plan.dbusAfterXTestBackspaceWaitMs =
      schedulerTiming.dbusAfterXTestBackspaceWaitMs;
  plan.ibusAfterXTestBackspaceWaitMs =
      schedulerTiming.ibusAfterXTestBackspaceWaitMs;
  plan.timerAccuracyUsec = schedulerTiming.timerAccuracyUsec;

  const auto status = backend->apply(
      inputContext, plan, [onDone](uint64_t, RewriteOutcome outcome) {
        onDone(outcome == RewriteOutcome::Succeeded);
      });
  if (status == ApplyStatus::Completed) {
    onDone(true);
    return true;
  }
  if (status == ApplyStatus::Failed) {
    onDone(false);
    return false;
  }
  return true;
}

void ArecaEngine::notifyUinputUnavailable() {
  auto *notifications = instance_->addonManager().addon("notifications", true);
  if (!notifications) {
    FCITX_WARN() << "areca: cannot show uinput permission notification: "
                    "notifications addon is unavailable";
    return;
  }
  notifications->call<fcitx::INotifications::sendNotification>(
      "Areca", 0, "fcitx-areca", "Areca không thể khởi tạo uinput",
      "Hãy kiểm tra quyền truy cập /dev/uinput và bảo đảm module uinput đã "
      "được bật, sau đó khởi động lại Fcitx5. Xem hướng dẫn khởi nạp uinput "
      "và cấp quyền tại https://github.com/xhkzeroone/ArecaIME#khởi-nạp-"
      "uinput-và-cấp-quyền",
      std::vector<std::string>{}, 15000, fcitx::NotificationActionCallback{},
      fcitx::NotificationClosedCallback{});
}

void ArecaEngine::scheduleDeviceWarmup() {
  deviceWarmupTimer_.reset();
  const uint64_t deadline = fcitx::now(CLOCK_MONOTONIC);
  deviceWarmupTimer_ = instance_->eventLoop().addTimeEvent(
      CLOCK_MONOTONIC, deadline, 0, [this](fcitx::EventSourceTime *, uint64_t) {
        auto timer = std::move(deviceWarmupTimer_);
        // Warmup chỉ chuẩn bị thiết bị. Chỉ báo lỗi khi routing thực sự thử
        // chọn uinput cho một thao tác gõ/xoá.
        const bool uinputAvailable = uinputDevice_.warmUp();
        // Chỉ đọc thuộc tính portal, không xin quyền, để máy không có libei
        // khỏi mở phiên Native khi cần dự phòng cho uinput. Phiên libei (và
        // hộp thoại xin quyền) chỉ mở ở lần đầu thật sự gửi phím qua Native.
        nativeDevice_.probeSupport();
        // Dò phím ảo sớm để menu chọn backend biết compositor có hỗ trợ không;
        // việc dò không mở hộp thoại nào.
        const bool virtualKeyboardStarted = virtualKeyboardDevice_.warmUp();
        if (debugEnabled()) {
          FCITX_INFO() << "areca: device warmup completed"
                       << " uinput-available=" << uinputAvailable
                       << " virtual-keyboard-started="
                       << virtualKeyboardStarted;
        }
        return false;
      });
  if (deviceWarmupTimer_) {
    deviceWarmupTimer_->setOneShot();
  }
}

PresentationMode
ArecaEngine::effectivePresentationMode(fcitx::InputContext &inputContext) {
  auto *state = inputContext.propertyFor(&rewriteStateFactory_);
  const std::string program = resolveProgram(inputContext, state);
  if (!program.empty()) {
    const auto it = appBackendOverridesMap_.find(program);
    if (it != appBackendOverridesMap_.end()) {
      return presentationModeForAppSetting(effectiveAppBackendMode(it->second),
                                           activePresentationMode_);
    }
  }
  return activePresentationMode_;
}

InputModeHandler &ArecaEngine::handlerFor(fcitx::InputContext &inputContext) {
  switch (effectivePresentationMode(inputContext)) {
  case PresentationMode::Preedit:
    return preeditHandler_;
  case PresentationMode::Redirect:
    return redirectHandler_;
  case PresentationMode::Rewrite:
    return rewriteHandler_;
  }
  return rewriteHandler_;
}

const char *ArecaEngine::presentationModeName(PresentationMode mode) {
  switch (mode) {
  case PresentationMode::Rewrite:
    return "Rewrite";
  case PresentationMode::Preedit:
    return "Preedit";
  case PresentationMode::Redirect:
    return "Redirect (EN)";
  }
  return "Rewrite";
}

std::string ArecaEngine::subMode(const fcitx::InputMethodEntry &,
                                 fcitx::InputContext &inputContext) {
  return presentationModeName(effectivePresentationMode(inputContext));
}

std::string ArecaEngine::subModeIconImpl(const fcitx::InputMethodEntry &entry,
                                         fcitx::InputContext &) {
  return overrideIcon(entry);
}

std::string ArecaEngine::overrideIcon(const fcitx::InputMethodEntry &) {
  const auto &icon = config_.trayIcon.value();
  return icon.empty() ? "org.fcitx.Fcitx5.fcitx-areca" : icon;
}

std::string ArecaEngine::subModeLabelImpl(const fcitx::InputMethodEntry &,
                                          fcitx::InputContext &inputContext) {
  return "Ă  " + config_.bambooInputMethod.value() + " \xC2\xB7 " +
         presentationModeName(effectivePresentationMode(inputContext));
}

void ArecaEngine::switchPresentationMode(fcitx::InputContext &inputContext) {
  if (scheduler_.rewritePending()) {
    if (debugEnabled()) {
      FCITX_INFO() << "areca: mode hotkey ignored while rewrite pending";
    }
    return;
  }

  const auto current = activePresentationMode_;
  PresentationMode next = PresentationMode::Rewrite;
  switch (current) {
  case PresentationMode::Rewrite:
    next = PresentationMode::Preedit;
    break;
  case PresentationMode::Preedit:
    next = PresentationMode::Redirect;
    break;
  case PresentationMode::Redirect:
    next = PresentationMode::Rewrite;
    break;
  }

  rewriteHandler_.resetContext(inputContext);
  preeditHandler_.resetContext(inputContext);
  config_.presentationMode.setValue(next);
  applyConfig();
  handlerFor(inputContext).activate(inputContext);
  save();

  if (debugEnabled()) {
    FCITX_INFO() << "areca: switch mode hotkey mode="
                 << presentationModeName(next)
                 << " program=" << inputContext.program();
  }
  instance_->showInputMethodInformation(&inputContext);
  updateUI(&inputContext);
}

void ArecaEngine::activate(const fcitx::InputMethodEntry &entry,
                           fcitx::InputContextEvent &event) {
  auto *inputContext = event.inputContext();
  if (!inputContext) {
    return;
  }
  syncInputMethodEntryIcon(entry);
  if (focusTracker_)
    focusTracker_->requestAccessibilityPoke();
  const PresentationMode presentationMode =
      effectivePresentationMode(*inputContext);
  if (presentationMode == PresentationMode::Rewrite) {
    clearBackendVerdictForLifecycle(*inputContext, "activate");
  }
  // Thử lại nếu helper đã mất kết nối. Bỏ click đã nhận trước activation để
  // thao tác ở context cũ không làm reset trạng thái của context vừa kích hoạt.
  if (mouseTracker_) {
    mouseTracker_->start();
    if (mouseTracker_->hasPendingClick() && debugEnabled())
      FCITX_INFO() << "areca: discard pending mouse click on activation";
    mouseTracker_->clearPendingClick();
  }
  handlerFor(*inputContext).activate(*inputContext);
  if (debugEnabled()) {
    FCITX_INFO() << "areca: activate presentation_mode="
                 << presentationModeName(presentationMode)
                 << " program=" << inputContext->program();
  }
  auto &statusArea = inputContext->statusArea();
  statusArea.addAction(fcitx::StatusGroup::InputMethod,
                       inputMethodAction_.get());
  statusArea.addAction(fcitx::StatusGroup::InputMethod, charsetAction_.get());
  // statusArea.addAction(fcitx::StatusGroup::InputMethod,
  //                      presentationModeAction_.get());
  statusArea.addAction(fcitx::StatusGroup::InputMethod,
                       spellcheckAction_.get());
  statusArea.addAction(fcitx::StatusGroup::InputMethod,
                       modernStyleAction_.get());
  statusArea.addAction(fcitx::StatusGroup::InputMethod, macroAction_.get());
  statusArea.addAction(fcitx::StatusGroup::InputMethod,
                       autoCapitalizeAction_.get());
  statusArea.addAction(fcitx::StatusGroup::InputMethod, settingsAction_.get());
  updateUI(inputContext);
}

void ArecaEngine::keyEvent(const fcitx::InputMethodEntry &,
                           fcitx::KeyEvent &event) {
  auto *inputContext = event.inputContext();
  if (!inputContext) {
    return;
  }
  if (inputContext->capabilityFlags().test(fcitx::CapabilityFlag::Password)) {
    if (isBackendSelecting(*inputContext)) {
      inputContext->inputPanel().reset();
      inputContext->updateUserInterface(
          fcitx::UserInterfaceComponent::InputPanel, true);
    }
    handlerFor(*inputContext).handleKeyEvent(event);
    return;
  }
  // Giống ô mật khẩu: terminal được chuyển tiếp nguyên phím, không qua Bamboo.
  if (config_.disableVietnameseInTerminal.value() &&
      inputTypeDetector_.isTerminal(
          resolveProgram(*inputContext,
                         inputContext->propertyFor(&rewriteStateFactory_)),
          inputContext->frontend())) {
    if (isBackendSelecting(*inputContext)) {
      inputContext->inputPanel().reset();
      inputContext->updateUserInterface(
          fcitx::UserInterfaceComponent::InputPanel, true);
    }
    if (!event.isRelease()) {
      handlerFor(*inputContext).resetContext(*inputContext);
    }
    event.forward();
    return;
  }
  if (isBackendSelecting(*inputContext)) {
    const auto normKey = event.key().normalize();
    if (!event.isRelease() &&
        normKey.checkKeyList(config_.selectBackendKey.value())) {
      inputContext->inputPanel().reset();
      inputContext->updateUserInterface(
          fcitx::UserInterfaceComponent::InputPanel, true);
      event.forward();
      return;
    }
    if (handleBackendSelectionKey(*inputContext, event)) {
      event.filterAndAccept();
      return;
    }
  }
  // Backend nhận phím inject trước RewriteMode để giữ nguyên state.
  if (uinputBackspaceBackend_.handleBackspace(event)) {
    return;
  }
  // Shift-select forward Left và chờ Shift up quay lại trước khi commit.
  if (uinputShiftSelectBackend_.handleKeyEvent(event)) {
    return;
  }
  // Phím ảo vừa gõ quay lại fcitx5: chuyển thẳng cho ứng dụng.
  if (virtualKeyboardBackspaceBackend_.handleKeyEvent(event)) {
    return;
  }
  if (!event.isRelease()) {
    // Resolve program một lần để nhận diện address bar dùng cùng verdict trong
    // toàn bộ lượt xử lý phím.
    auto *state = inputContext->propertyFor(&rewriteStateFactory_);
    resolveProgram(*inputContext, state);

    // Sau click, bỏ state của caret cũ để phím kế tiếp có thể phục hồi phần từ
    // nằm trước caret mới. Preedit chỉ làm việc này khi composition đã trống,
    // tránh làm mất chữ nếu frontend chưa kết thúc preedit đang hiển thị.
    if (mouseTracker_ && mouseTracker_->hasPendingClick()) {
      const auto mode = effectivePresentationMode(*inputContext);
      if (mode == PresentationMode::Preedit) {
        const bool prepared =
            preeditHandler_.prepareSurroundingRestoreAfterClick(*inputContext);
        if (debugEnabled())
          FCITX_INFO() << "areca: preedit mouse restore "
                       << (prepared ? "armed" : "skipped-active-composition");
        mouseTracker_->clearPendingClick();
      } else if (mode != PresentationMode::Rewrite) {
        if (debugEnabled())
          FCITX_INFO() << "areca: discard mouse reset mode="
                       << presentationModeName(mode);
        mouseTracker_->clearPendingClick();
      } else if (scheduler_.shouldRejectReset()) {
        // Giữ click cho lần sau nếu rewrite đang xóa/chèn chữ.
        if (debugEnabled())
          FCITX_INFO() << "areca: mouse reset deferred (rewrite protection)";
      } else {
        if (debugEnabled())
          FCITX_INFO() << "areca: applying mouse reset mode="
                       << presentationModeName(mode);
        rewriteHandler_.resetContext(*inputContext);
        clearBackendVerdictForLifecycle(*inputContext, "mouse-click");
        mouseTracker_->clearPendingClick();
      }
    }
    const auto key = event.key().normalize();
    // if (key.sym() != FcitxKey_None &&
    //     key.checkKeyList(config_.switchModeKey.value())) {
    //   if (inputContext->capabilityFlags().test(
    //           fcitx::CapabilityFlag::Password)) {
    //     handlerFor(*inputContext).handleKeyEvent(event);
    //     return;
    //   }
    //   event.filterAndAccept();
    //   switchPresentationMode(*inputContext);
    //   return;
    // }

    if (key.sym() != FcitxKey_None &&
        key.checkKeyList(config_.selectBackendKey.value())) {
      if (inputContext->capabilityFlags().test(
              fcitx::CapabilityFlag::Password)) {
        handlerFor(*inputContext).handleKeyEvent(event);
        return;
      }
      bool isComposing = false;
      const PresentationMode presentationMode =
          effectivePresentationMode(*inputContext);
      if (presentationMode == PresentationMode::Rewrite) {
        if (state && state->engine && !state->engine->currentText().empty()) {
          isComposing = true;
        }
      } else if (presentationMode == PresentationMode::Preedit) {
        auto *preState = inputContext->propertyFor(&preeditStateFactory_);
        if (preState && !preState->composing.empty()) {
          isComposing = true;
        }
      }
      if (!isComposing && !scheduler_.rewritePending()) {
        event.filterAndAccept();
        showBackendSelectionMenu(*inputContext);
        return;
      }
    }
  }
  handlerFor(*inputContext).handleKeyEvent(event);
}

void ArecaEngine::deactivate(const fcitx::InputMethodEntry &,
                             fcitx::InputContextEvent &event) {
  auto *inputContext = event.inputContext();
  if (!inputContext) {
    return;
  }
  backendSelectionPost_.cancel();
  if (isBackendSelecting(*inputContext)) {
    inputContext->inputPanel().reset();
    inputContext->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel,
                                      true);
  }
  const PresentationMode presentationMode =
      effectivePresentationMode(*inputContext);
  if (presentationMode == PresentationMode::Rewrite) {
    clearBackendVerdictForLifecycle(*inputContext, "deactivate");
  }
  handlerFor(*inputContext).deactivate(*inputContext);
}

void ArecaEngine::reset(const fcitx::InputMethodEntry &,
                        fcitx::InputContextEvent &event) {
  if (auto *inputContext = event.inputContext()) {
    if (scheduler_.shouldRejectReset()) {
      if (debugEnabled()) {
        FCITX_INFO() << "areca: app reset rejected (active rewrite or 50ms "
                        "post-commit window)";
      }
      return;
    }
    if (effectivePresentationMode(*inputContext) == PresentationMode::Rewrite) {
      clearBackendVerdictForLifecycle(*inputContext, "reset");
    }
    if (debugEnabled()) {
      FCITX_INFO() << "areca: app reset requested; arm protected reset";
    }
    handlerFor(*inputContext).requestProtectedReset(*inputContext);
  }
}

class ArecaEngineFactory final : public fcitx::AddonFactory {
public:
  fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
    return new ArecaEngine(manager->instance());
  }
};

} // namespace areca

FCITX_ADDON_FACTORY(areca::ArecaEngineFactory)
