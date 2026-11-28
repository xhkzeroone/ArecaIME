#include "areca_engine.h"

#include <algorithm>
#include <array>

#include "browser_autocomplete.h"
#include "program_compatibility.h"
#include <fcitx-utils/log.h>
#include <fcitx/inputcontext.h>
#include <fcitx/surroundingtext.h>

#include "mouse_click_tracker.h"
#include "window_focus_tracker.h"

namespace areca {

namespace {

constexpr uint64_t kBackendVerdictProtectionUsec = 1ULL * 1000 * 1000;

using SurroundingRule = bool (*)(const fcitx::SurroundingText &);

constexpr std::array<SurroundingRule, 3> kInvalidSurroundingRules = {
    [](const auto &s) { return s.cursor() <= 0; },
    [](const auto &s) { return s.anchor() <= 0; },
    [](const auto &s) { return s.text().empty(); }};

bool isInvalidSurrounding(const fcitx::SurroundingText &surrounding) {
  return std::any_of(kInvalidSurroundingRules.begin(),
                     kInvalidSurroundingRules.end(),
                     [&](auto rule) { return rule(surrounding); });
}

bool isGnomeShellProxyProgram(std::string_view program) {
  return program == "gnome-shell" || program == "mutter" ||
         program == "org.gnome.Shell" || program == "org.gnome.Mutter";
}

} // namespace

void ArecaEngine::protectBackendVerdict(fcitx::InputContext &inputContext,
                                        const char *reason) {
  if (!backendVerdictContextKnown_ ||
      backendVerdictContextId_ != inputContext.uuid() ||
      !backendVerdict_.known) {
    return;
  }
  backendVerdictProtectedUntil_ =
      fcitx::now(CLOCK_MONOTONIC) + kBackendVerdictProtectionUsec;
  if (debugEnabled()) {
    FCITX_INFO() << "areca: backend verdict protection armed"
                 << " reason=" << reason << " duration_ms=1000"
                 << " program=" << inputContext.program();
  }
}

bool ArecaEngine::surroundingBackendInUse(fcitx::InputContext &inputContext) {
  const auto program = resolveProgram(
      inputContext, inputContext.propertyFor(&rewriteStateFactory_));
  const auto it = surroundingBackendByApp_.find(program);
  // Chưa từng chọn backend cho app này thì chưa có lý do để tắt restore.
  return it == surroundingBackendByApp_.end() || it->second;
}

void ArecaEngine::noteRewriteBackendChoice(fcitx::InputContext &inputContext,
                                           const RewriteBackend *backend) {
  const auto program = resolveProgram(
      inputContext, inputContext.propertyFor(&rewriteStateFactory_));
  if (program.empty()) {
    return;
  }
  surroundingBackendByApp_[program] = backend == &surroundingBackend_;
}

bool ArecaEngine::backendVerdictProtected(
    fcitx::InputContext &inputContext) const {
  return backendVerdictContextKnown_ && backendVerdict_.known &&
         backendVerdictContextId_ == inputContext.uuid() &&
         backendVerdictProtectedUntil_ > fcitx::now(CLOCK_MONOTONIC);
}

void ArecaEngine::clearBackendVerdictForLifecycle(
    fcitx::InputContext &inputContext, const char *eventName) {
  if (!backendVerdictContextKnown_ ||
      backendVerdictContextId_ != inputContext.uuid()) {
    return;
  }
  if (std::string_view(eventName) != "reset" &&
      backendVerdictProtected(inputContext)) {
    if (debugEnabled()) {
      FCITX_INFO() << "areca: backend verdict cache preserved"
                   << " event=" << eventName
                   << " program=" << inputContext.program();
    }
    return;
  }
  backendVerdict_.reset();
  backendVerdictProtectedUntil_ = 0;
  if (std::string_view(eventName) == "reset") {
    const std::string prog = resolveProgram(
        inputContext, inputContext.propertyFor(&rewriteStateFactory_));
    if (!prog.empty()) {
      lastAutoBackendByApp_.erase(prog);
    }
  }
  if (debugEnabled()) {
    FCITX_INFO() << "areca: backend verdict cache cleared"
                 << " event=" << eventName
                 << " program=" << inputContext.program();
  }
}

std::string ArecaEngine::resolveProgram(fcitx::InputContext &inputContext,
                                        RewriteInputState *state) {
  const std::string reportedProgram = inputContext.program();
  const bool needsTrackedProgram =
      reportedProgram.empty() || isGnomeShellProxyProgram(reportedProgram);
  if (!needsTrackedProgram) {
    return reportedProgram;
  }

  if (focusTracker_ && focusTracker_->isValid()) {
    const std::string trackedProgram = focusTracker_->focusProgram();
    if (!trackedProgram.empty() && !isGnomeShellProxyProgram(trackedProgram)) {
      if (state) {
        state->resolvedProgram = trackedProgram;
      }
      if (debugEnabled()) {
        FCITX_INFO()
            << "areca: resolved and cached proxy program via focus tracker"
            << " reported=" << reportedProgram << " tracked=" << trackedProgram;
      }
      return trackedProgram;
    }
  }

  if (state && !state->resolvedProgram.empty()) {
    return state->resolvedProgram;
  }
  return reportedProgram;
}

RewriteBackend *ArecaEngine::resolvePreferredSurroundingBackend() {
  return &surroundingBackend_;
}

void ArecaEngine::handleSurroundingProbeSucceeded(
    fcitx::InputContext &inputContext) {
  backendVerdict_.probePending = false;
  backendVerdict_.retryOnNextWord = false;
  if (debugEnabled()) {
    FCITX_INFO() << "areca: surrounding probe succeeded program="
                 << inputContext.program();
  }
}

void ArecaEngine::handleSurroundingProbeFailed(
    fcitx::InputContext &inputContext) {
  auto *state = inputContext.propertyFor(&rewriteStateFactory_);
  const uint32_t wordSerial = state ? state->wordSerial : 0;
  backendVerdict_.probeFailed(wordSerial);
  // Transaction này đã hoàn tất bằng backend dự phòng.
  noteRewriteBackendChoice(inputContext, nullptr);
  if (debugEnabled()) {
    FCITX_INFO() << "areca: surrounding probe failed, marked unreliable"
                 << " attempt=" << backendVerdict_.probeAttempts
                 << " retry_next_word=" << backendVerdict_.retryOnNextWord
                 << " word=" << wordSerial
                 << " program=" << inputContext.program();
  }
}

RewriteBackend *ArecaEngine::resolveSurroundingFallbackBackend(
    fcitx::InputContext &inputContext) {
  const std::string prog = resolveProgram(
      inputContext, inputContext.propertyFor(&rewriteStateFactory_));
  const char *frontend = inputContext.frontend();
  const bool isTerminal = inputTypeDetector_.isTerminal(prog, frontend);
  const bool canForward =
      inputTypeDetector_.canForwardBackspaces(inputContext, prog);
  return resolvePreferredBackspaceBackend(isTerminal || !canForward);
}

RewriteBackend *ArecaEngine::resolvePreferredShiftSelectBackend() {
  if (advancedConfig_.useNativeInsteadOfUinput.value() &&
      nativeShiftSelectBackend_.isAvailable()) {
    return &nativeShiftSelectBackend_;
  }
  if (uinputShiftSelectBackend_.isAvailable()) {
    return &uinputShiftSelectBackend_;
  }
  if (nativeShiftSelectBackend_.isAvailable()) {
    return &nativeShiftSelectBackend_;
  }
  return nullptr;
}

RewriteBackend *
ArecaEngine::resolvePreferredBackspaceBackend(bool preferHardware) {
  if (preferHardware || advancedConfig_.useUinputInsteadOfForwardKey.value()) {
    if (advancedConfig_.useNativeInsteadOfUinput.value() &&
        nativeBackspaceBackend_.isAvailable()) {
      return &nativeBackspaceBackend_;
    }
    if (uinputBackspaceBackend_.isAvailable()) {
      return &uinputBackspaceBackend_;
    }
    if (nativeBackspaceBackend_.isAvailable()) {
      return &nativeBackspaceBackend_;
    }
  }
  return &forwardBackspaceBackend_;
}

RewriteBackend *
ArecaEngine::resolveUnmarkedBackspaceBackend(bool preferHardware) {
  RewriteBackend *backend = resolvePreferredBackspaceBackend(preferHardware);
  if (backend == &uinputBackspaceBackend_) {
    return &unmarkedUinputBackspaceBackend_;
  }
  if (backend == &nativeBackspaceBackend_) {
    return &unmarkedNativeBackspaceBackend_;
  }
  return &unmarkedForwardBackspaceBackend_;
}

bool ArecaEngine::isBackendModeOffered(AppBackendMode mode) {
  switch (mode) {
  case AppBackendMode::VirtualKeyboard:
    return virtualKeyboardDevice_.isAvailable();
  case AppBackendMode::Native:
    // Native chỉ còn là tuỳ chọn nâng cao thay uinput.
    return false;
  default:
    return true;
  }
}

AppBackendMode ArecaEngine::effectiveAppBackendMode(AppBackendMode saved) {
  return isBackendModeOffered(saved) ? saved : AppBackendMode::Auto;
}

std::string ArecaEngine::describeAutoBackend(const std::string &appName) {
  auto it = lastAutoBackendByApp_.find(appName);
  if (it != lastAutoBackendByApp_.end()) {
    switch (it->second) {
    case AppBackendMode::SurroundingText:
      return "Tự động (Surrounding)";
    case AppBackendMode::ShiftSelect:
      return "Tự động (Shift select)";
    case AppBackendMode::Backspace:
      return "Tự động (Backspace)";
    case AppBackendMode::Preedit:
    case AppBackendMode::Redirect:
      break;
    default:
      break;
    }
  }
  return "Tự động";
}

// Tên backend cho menu đi theo thứ tự của bộ chọn nhưng chỉ hỏi
// nativeDevice_.isSupported(): isAvailable() của Native mở phiên libei và bật
// hộp thoại xin quyền, chỉ được gọi khi thật sự gửi phím.
std::string ArecaEngine::describeShiftSelectBackend() {
  const bool nativeSupported = nativeDevice_.isSupported();
  if (advancedConfig_.useNativeInsteadOfUinput.value() && nativeSupported) {
    return "Native";
  }
  if (uinputShiftSelectBackend_.isAvailable()) {
    return "Uinput";
  }
  if (nativeSupported) {
    return "Native";
  }
  return advancedConfig_.useNativeInsteadOfUinput.value() ? "Native" : "Uinput";
}

std::string ArecaEngine::describeBackspaceBackend(bool preferHardware) {
  if (preferHardware || advancedConfig_.useUinputInsteadOfForwardKey.value()) {
    const bool nativeSupported = nativeDevice_.isSupported();
    if (advancedConfig_.useNativeInsteadOfUinput.value() && nativeSupported) {
      return "Native";
    }
    if (uinputBackspaceBackend_.isAvailable()) {
      return "Uinput";
    }
    if (nativeSupported) {
      return "Native";
    }
  }
  return "ForwardKey";
}

RewriteBackendSelection
ArecaEngine::selectRewriteBackend(fcitx::InputContext &inputContext,
                                  const BambooResult &result) {
  auto *state = inputContext.propertyFor(&rewriteStateFactory_);
  if (!state) {
    return {resolvePreferredBackspaceBackend(false)};
  }

  const std::string program = resolveProgram(inputContext, state);

  const char *frontend = inputContext.frontend();
  const bool isTerminal = inputTypeDetector_.isTerminal(program, frontend);
  const bool canForward =
      inputTypeDetector_.canForwardBackspaces(inputContext, program);
  const bool preferHardware = isTerminal || !canForward;

  if (!backendVerdictContextKnown_ ||
      backendVerdictContextId_ != inputContext.uuid()) {
    backendVerdictContextKnown_ = true;
    backendVerdictContextId_ = inputContext.uuid();
    backendVerdict_.reset();
    backendVerdictProtectedUntil_ = 0;
    if (debugEnabled()) {
      FCITX_INFO() << "areca: backend verdict cache switched context"
                   << " program=" << inputContext.program();
    }
  }

  if (isTerminal) {
    RewriteBackend *backend = resolvePreferredBackspaceBackend(true);
    if (!program.empty()) {
      lastAutoBackendByApp_[program] = AppBackendMode::Backspace;
    }
    if (debugEnabled()) {
      FCITX_INFO() << "areca: terminal selected backend=" << backend->name()
                   << " program=" << program;
    }
    return {backend};
  }

  const auto &surrounding = inputContext.surroundingText();
  // Chỉ tin CapabilityFlag::Url, không đoán thanh địa chỉ qua trợ năng.
  const bool isUrl =
      inputContext.capabilityFlags().test(fcitx::CapabilityFlag::Url);
  const auto decision =
      evaluateReliability(inputContext, result.currentText, program);
  const bool hasActiveSelection =
      surrounding.isValid() && surrounding.cursor() != surrounding.anchor();
  const bool collapsedFirefoxAutocomplete =
      surrounding.isValid() &&
      isCollapsedFirefoxAutocomplete(program, surrounding.text(),
                                     surrounding.cursor(), surrounding.anchor(),
                                     result.currentText);

  // Gợi ý tự hoàn thành hay vùng chọn sau con trỏ: Backspace có marker, "·"
  // thay phần đang được chọn rồi bị xoá cùng từ cũ. Chỉ xử lý khi frontend
  // cung cấp surrounding text đủ để quan sát selection, không đoán gợi ý khi
  // surrounding không khả dụng.
  if (decision.browserAutocomplete || collapsedFirefoxAutocomplete ||
      hasActiveSelection) {
    RewriteBackend *backend = resolvePreferredBackspaceBackend(preferHardware);
    if (!program.empty()) {
      lastAutoBackendByApp_[program] = AppBackendMode::Backspace;
    }
    if (debugEnabled()) {
      FCITX_INFO() << "areca: browser autocomplete strategy=" << backend->name()
                   << " is_url=" << isUrl
                   << " collapsed_autocomplete=" << collapsedFirefoxAutocomplete
                   << " active_selection=" << hasActiveSelection
                   << " bamboo_delete=" << result.deleteCount;
    }
    return {backend};
  }

  // Ô URL luôn xoá bằng Backspace. Marker vẫn được commit để cắt gợi ý mà
  // surrounding text không cho thấy.
  if (isUrl) {
    RewriteBackend *backend = resolvePreferredBackspaceBackend(preferHardware);
    if (!program.empty()) {
      lastAutoBackendByApp_[program] = AppBackendMode::Backspace;
    }
    if (debugEnabled()) {
      FCITX_INFO() << "areca: url selected backend=" << backend->name()
                   << " program=" << program;
    }
    return {backend};
  }

  if (!program.empty()) {
    auto it = appBackendOverridesMap_.find(program);
    const AppBackendMode appMode = it != appBackendOverridesMap_.end()
                                       ? effectiveAppBackendMode(it->second)
                                       : AppBackendMode::Auto;
    if (appMode != AppBackendMode::Auto) {
      switch (appMode) {
      case AppBackendMode::SurroundingText:
        if (decision.useSurrounding) {
          auto *surBackend = resolvePreferredSurroundingBackend();
          if (backendVerdict_.probePending) {
            backendVerdict_.probeStarted();
          }
          const uint32_t probeTimeoutMs =
              backendVerdict_.probePending
                  ? backendVerdict_.activeProbeTimeoutMs()
                  : 0;
          if (debugEnabled()) {
            FCITX_INFO() << "areca: app-override selected surrounding backend="
                         << surBackend->name() << " program=" << program
                         << " probe_timeout_ms=" << probeTimeoutMs;
          }
          return {surBackend, backendVerdict_.probePending, probeTimeoutMs};
        }
        return {resolvePreferredBackspaceBackend(preferHardware)};
      case AppBackendMode::ShiftSelect:
        if (!isTerminal) {
          if (auto *shiftBackend = resolvePreferredShiftSelectBackend()) {
            if (debugEnabled()) {
              FCITX_INFO()
                  << "areca: app-override selected shift-select backend="
                  << shiftBackend->name() << " program=" << program;
            }
            return {shiftBackend};
          }
        }
        return {resolvePreferredBackspaceBackend(preferHardware)};
      case AppBackendMode::Backspace: {
        auto *bsBackend = resolvePreferredBackspaceBackend(preferHardware);
        if (debugEnabled()) {
          FCITX_INFO() << "areca: app-override selected backspace backend="
                       << bsBackend->name() << " program=" << program;
        }
        return {bsBackend};
      }
      case AppBackendMode::BackspaceUnmarked: {
        auto *bsBackend = resolveUnmarkedBackspaceBackend(preferHardware);
        if (debugEnabled()) {
          FCITX_INFO() << "areca: app-override selected backspace backend="
                       << bsBackend->name() << " program=" << program;
        }
        return {bsBackend};
      }
      case AppBackendMode::VirtualKeyboard: {
        if (virtualKeyboardBackspaceBackend_.isAvailable()) {
          if (debugEnabled()) {
            FCITX_INFO()
                << "areca: app-override selected virtual-keyboard-backspace"
                << " program=" << program;
          }
          return {&virtualKeyboardBackspaceBackend_};
        }
        // Kết nối phím ảo vừa hỏng giữa chừng: dùng như khi chọn Backspace.
        auto *bsBackend = resolvePreferredBackspaceBackend(preferHardware);
        if (debugEnabled()) {
          FCITX_INFO()
              << "areca: virtual-keyboard-backspace unavailable, fallback "
              << "backend=" << bsBackend->name() << " program=" << program;
        }
        return {bsBackend};
      }
      case AppBackendMode::Native:
      case AppBackendMode::Preedit:
      case AppBackendMode::Redirect:
        // Mode routing happens before key handling, so these entries must not
        // normally reach the rewrite scheduler. Fall through to auto as a
        // safe fallback if an already queued transaction outlives a switch.
        break;
      case AppBackendMode::Auto:
        break;
      }
    }
  }

  if (decision.useSurrounding) {
    auto *surBackend = resolvePreferredSurroundingBackend();
    if (backendVerdict_.probePending) {
      backendVerdict_.probeStarted();
    }
    const uint32_t probeTimeoutMs = backendVerdict_.probePending
                                        ? backendVerdict_.activeProbeTimeoutMs()
                                        : 0;
    if (!program.empty()) {
      lastAutoBackendByApp_[program] = AppBackendMode::SurroundingText;
    }
    if (debugEnabled()) {
      FCITX_INFO() << "areca: auto selected surrounding backend="
                   << surBackend->name() << " program=" << program
                   << " probe=" << backendVerdict_.probePending
                   << " probe_timeout_ms=" << probeTimeoutMs;
    }
    return {surBackend, backendVerdict_.probePending, probeTimeoutMs};
  }

  if (!program.empty()) {
    lastAutoBackendByApp_[program] = AppBackendMode::Backspace;
  }
  return {resolvePreferredBackspaceBackend(preferHardware)};
}

ReliabilityDecision
ArecaEngine::evaluateReliability(fcitx::InputContext &inputContext,
                                 const std::string &shownText,
                                 const std::string &program) {
  const auto &surrounding = inputContext.surroundingText();
  const std::string &programName =
      !program.empty() ? program : inputContext.program();

  const bool browserAutocomplete =
      inputTypeDetector_.isBrowser(programName) &&
      isBrowserAutocomplete(surrounding.text(), surrounding.cursor(),
                            surrounding.anchor(), shownText);

  // Bắt đầu: Probe lại đúng một lần khi bước sang từ kế tiếp
  auto *state = inputContext.propertyFor(&rewriteStateFactory_);
  if (!browserAutocomplete && state &&
      backendVerdict_.retryForWord(state->wordSerial) && debugEnabled()) {
    FCITX_INFO() << "areca: surrounding probe retry armed"
                 << " attempt=" << (backendVerdict_.probeAttempts + 1)
                 << " word=" << state->wordSerial << " program=" << programName;
  }
  // Kết thúc: Probe lại đúng một lần khi bước sang từ kế tiếp

  if (!backendVerdict_.known && !browserAutocomplete) {
    backendVerdict_.reliable = !isInvalidSurrounding(surrounding);
    backendVerdict_.probePending = backendVerdict_.reliable;
    if (debugEnabled()) {
      FCITX_INFO() << "areca: reliability first-probe text="
                   << surrounding.text() << " cursor=" << surrounding.cursor()
                   << " anchor=" << surrounding.anchor()
                   << " reliable=" << backendVerdict_.reliable
                   << " probe_pending=" << backendVerdict_.probePending;
    }

    backendVerdict_.forceForwardBackspace = false;
    if (isProgrammingProgram(programName) || isTerminalProgram(programName) ||
        isOfficeProgram(programName)) {
      backendVerdict_.forceForwardBackspace = true;
      if (debugEnabled()) {
        FCITX_INFO() << "areca: reliability first-probe force_forward=1"
                     << " reason=programming-terminal-or-office-program"
                     << " program=" << programName;
      }
    } else if (backendVerdict_.reliable && isVSCodeFamilyProgram(programName)) {
      const uint64_t capabilityMask =
          inputContext.capabilityFlags().toInteger();
      backendVerdict_.forceForwardBackspace = (capabilityMask == 0x72);
      if (backendVerdict_.forceForwardBackspace && debugEnabled()) {
        FCITX_INFO() << "areca: reliability first-probe force_forward=1"
                     << " reason=program-compatibility-capability-mask-0x72"
                     << " program=" << programName;
      }
    }
    backendVerdict_.known = true;
  }

  // Snapshot trễ lúc thăm dò không còn làm hỏng probe: backend surrounding chờ
  // snapshot có đủ từ đang gõ rồi mới xoá, quá giờ thì lượt đó dùng Backspace.

  if (debugEnabled()) {
    FCITX_INFO() << "areca: reliability cached known=" << backendVerdict_.known
                 << " reliable=" << backendVerdict_.reliable
                 << " force_forward=" << backendVerdict_.forceForwardBackspace
                 << " browser_autocomplete=" << browserAutocomplete
                 << " program=" << programName;
  }

  ReliabilityDecision decision;
  decision.browserAutocomplete = browserAutocomplete;
  decision.useSurrounding = backendVerdict_.known && backendVerdict_.reliable &&
                            !backendVerdict_.forceForwardBackspace &&
                            !browserAutocomplete;
  return decision;
}

bool ArecaEngine::backspaceRecoveryEnabled() const {
  return config_.backspaceRecovery.value();
}

} // namespace areca
