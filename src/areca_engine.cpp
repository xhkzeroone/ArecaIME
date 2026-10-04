#include "areca_engine.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <utility>

#include <fcitx-config/iniparser.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#if __has_include(<fcitx-utils/standardpaths.h>)
#include <fcitx-utils/standardpaths.h>
#define ARECA_HAS_STANDARD_PATHS 1
#else
#include <fcitx-utils/standardpath.h>
#endif
#include <fcitx-utils/misc.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/statusarea.h>
#include <fcitx/surroundingtext.h>

#include "browser_autocomplete.h"
#include "program_compatibility.h"

#include "mouse_click_tracker.h"
#include "window_focus_tracker.h"

namespace areca {
namespace {

using SurroundingRule = bool (*)(const fcitx::SurroundingText &);

constexpr std::array<SurroundingRule, 4> kInvalidSurroundingRules = {
    [](const auto &s) { return s.cursor() <= 0; },
    [](const auto &s) { return s.anchor() <= 0; },
    [](const auto &s) { return s.text().empty(); },
    [](const auto &s) { return s.text() == "_"; },
};

bool isInvalidSurrounding(const fcitx::SurroundingText &surrounding) {
  return std::any_of(kInvalidSurroundingRules.begin(),
                     kInvalidSurroundingRules.end(),
                     [&](auto rule) { return rule(surrounding); });
}

constexpr const char *kMacroConfigPath = "conf/areca-macro-table.conf";
constexpr const char *kAppsConfigPath = "conf/areca-apps.conf";
constexpr const char *kAdvancedConfigPath = "conf/areca-advanced.conf";
constexpr uint64_t kBackendVerdictProtectionUsec = 1ULL * 1000 * 1000;
#if defined(ARECA_HAS_STANDARD_PATHS)
constexpr auto kPkgConfigPath = fcitx::StandardPathsType::PkgConfig;
#else
constexpr auto kPkgConfigPath = fcitx::StandardPath::Type::PkgConfig;
#endif

class BackendCandidateWord : public fcitx::CandidateWord {
public:
  BackendCandidateWord(fcitx::Text text, AppBackendMode mode,
                       std::function<void(AppBackendMode)> onSelect)
      : fcitx::CandidateWord(std::move(text)), mode_(mode),
        onSelect_(std::move(onSelect)) {}

  void select(fcitx::InputContext *) const override {
    if (onSelect_) {
      onSelect_(mode_);
    }
  }

  AppBackendMode mode() const { return mode_; }

private:
  AppBackendMode mode_;
  std::function<void(AppBackendMode)> onSelect_;
};

} // namespace

ArecaEngine::ArecaEngine(fcitx::Instance *instance)
    : instance_(instance), rewriteStateFactory_([this](fcitx::InputContext &) {
        return new RewriteInputState(
            config_.bambooInputMethod.value(),
            config_.spellcheckMode.value() != SpellcheckMode::Off,
            config_.spellcheckMode.value() == SpellcheckMode::Realtime,
            config_.modernStyle.value(), config_.outputCharset.value(),
            config_.enableMacro.value(), config_.capitalizeMacro.value(),
            macroRevision_, macroDefinitions());
      }),
      preeditStateFactory_([this](fcitx::InputContext &) {
        return new PreeditInputState(
            config_.bambooInputMethod.value(),
            config_.spellcheckMode.value() != SpellcheckMode::Off,
            config_.spellcheckMode.value() == SpellcheckMode::Realtime,
            config_.modernStyle.value(), config_.outputCharset.value(),
            config_.enableMacro.value(), config_.capitalizeMacro.value(),
            macroRevision_, macroDefinitions());
      }),
      adaptiveWaitMonitor_(instance_->eventLoop(), adaptiveWait_,
                           [this]() { return debugEnabled(); }),
      surroundingBackend_(instance_->eventLoop(),
                          [this]() { return debugEnabled(); }),
      surroundingV2Backend_(instance_->eventLoop(),
                            [this]() { return debugEnabled(); }),
      forwardBackspaceBackend_(instance_->eventLoop(), adaptiveWait_,
                               [this]() { return debugEnabled(); }),
      uinputDevice_([this]() { return debugEnabled(); }),
      uinputBackspaceBackend_(instance_->eventLoop(), uinputDevice_,
                              adaptiveWait_,
                              [this]() { return debugEnabled(); }),
      uinputShiftSelectBackend_(instance_->eventLoop(), uinputDevice_,
                                [this]() { return debugEnabled(); }),
      nativeDevice_(instance_->eventLoop(),
                    [this]() { return debugEnabled(); }),
      xtestBackspaceBackend_(instance_->eventLoop(), nativeDevice_,
                             adaptiveWait_,
                             [this]() { return debugEnabled(); }),
      scheduler_(
          instance_->eventLoop(),
          [this](fcitx::InputContext &inputContext) -> VietnameseEngine * {
            auto *state = inputContext.propertyFor(&rewriteStateFactory_);
            return state ? state->engine.get() : nullptr;
          },
          [this]() { return timing(); }, [this]() { return debugEnabled(); },
          [this](fcitx::InputContext &inputContext,
                 const BambooResult &result) -> RewriteBackendSelection {
            return selectRewriteBackend(inputContext, result);
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
          [this]() { return config_.restoreSurroundingText.value(); }),
      preeditHandler_(
          instance_->eventLoop(), preeditStateFactory_,
          [this]() { return debugEnabled(); },
          [this]() { return config_.autoCapitalizeAfterPunctuation.value(); },
          [this]() { return backspaceRecoveryEnabled(); },
          [this]() { return config_.restoreSurroundingText.value(); }) {
  instance_->inputContextManager().registerProperty("arecaRewriteState",
                                                    &rewriteStateFactory_);
  instance_->inputContextManager().registerProperty("arecaPreeditState",
                                                    &preeditStateFactory_);
  surroundingTextWatcher_ = instance_->watchEvent(
      fcitx::EventType::InputContextSurroundingTextUpdated,
      fcitx::EventWatcherPhase::PostInputMethod, [this](fcitx::Event &event) {
        auto &contextEvent = static_cast<fcitx::InputContextEvent &>(event);
        auto *inputContext = contextEvent.inputContext();
        if (!inputContext) {
          return;
        }
        // Callback chỉ ghi nhận rằng frontend đã đổi text hoặc cursor. Việc đọc
        // và phục hồi được hoãn tới key press để không sửa Bamboo trong event
        // lifecycle và để nhận snapshot surrounding mới nhất.
        if (auto *state = rewriteHandler_.stateFor(*inputContext)) {
          state->surroundingRestoreArmed = true;
        }
        if (auto *state = preeditHandler_.stateFor(*inputContext)) {
          state->surroundingRestoreArmed = true;
        }
      });
  config_.bambooInputMethod.annotation().setList(
      BambooEngineAdapter::inputMethodNames());
  config_.outputCharset.annotation().setList(
      BambooEngineAdapter::charsetNames());
  reloadConfig();
  focusTracker_ = std::make_unique<WindowFocusTracker>();
  if (!focusTracker_->start() || !focusTracker_->isValid()) {
    if (debugEnabled()) {
      FCITX_INFO() << "areca: window focus tracker inactive, disabled";
    }
    focusTracker_->stop();
    focusTracker_.reset();
  }
  inputTypeDetector_.setFocusTracker(focusTracker_.get());

  settingsAction_ = std::make_unique<fcitx::SimpleAction>();
  settingsAction_->setShortText("Areca Settings");
  settingsAction_->setIcon("configure");
  settingsAction_->connect<fcitx::SimpleAction::Activated>(
      [](fcitx::InputContext *) {
        fcitx::startProcess({ARECA_SETTINGS_PATH});
      });
  settingsAction_->registerAction("areca-settings",
                                  &instance_->userInterfaceManager());
}

ArecaEngine::~ArecaEngine() {
  inputTypeDetector_.setFocusTracker(nullptr);
  if (focusTracker_) {
    focusTracker_->stop();
  }
}

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
  if (backendVerdictProtected(inputContext)) {
    if (debugEnabled()) {
      FCITX_INFO() << "areca: backend verdict cache preserved"
                   << " event=" << eventName
                   << " program=" << inputContext.program();
    }
    return;
  }
  backendVerdict_.reset();
  backendVerdictProtectedUntil_ = 0;
  if (debugEnabled()) {
    FCITX_INFO() << "areca: backend verdict cache cleared"
                 << " event=" << eventName
                 << " program=" << inputContext.program();
  }
}

std::string ArecaEngine::resolveProgram(fcitx::InputContext &inputContext,
                                        RewriteInputState *state) {
  std::string program = inputContext.program();
  if (!program.empty()) {
    return program;
  }
  if (state && !state->resolvedProgram.empty()) {
    return state->resolvedProgram;
  }
  if (focusTracker_ && focusTracker_->isValid()) {
    program = focusTracker_->focusProgram();
    if (!program.empty()) {
      if (state) {
        state->resolvedProgram = program;
      }
      if (debugEnabled()) {
        FCITX_INFO()
            << "areca: resolved and cached empty program via focus tracker: "
            << program;
      }
      return program;
    }
  }
  return {};
}

bool ArecaEngine::inChromiumAddressBar(fcitx::InputContext &inputContext,
                                       const std::string &program,
                                       RewriteInputState *state) {
  uint64_t *verdictUsec = state ? &state->addrBarUiVerdictAtUsec : nullptr;
  return inputTypeDetector_.inChromiumAddressBar(inputContext, program,
                                                 verdictUsec);
}

RewriteBackendSelection
ArecaEngine::selectRewriteBackend(fcitx::InputContext &inputContext,
                                  const BambooResult &result) {
  auto *state = inputContext.propertyFor(&rewriteStateFactory_);
  const std::string program = resolveProgram(inputContext, state);

  if (!state) {
    if (advancedConfig_.useXTestInsteadOfUinput.value() &&
        xtestBackspaceBackend_.isAvailable()) {
      return {&xtestBackspaceBackend_};
    }
    return {&forwardBackspaceBackend_};
  }

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

  const char *frontend = inputContext.frontend();

  const bool isTerminal = inputTypeDetector_.isTerminal(program, frontend);

  if (isTerminal) {
    if (advancedConfig_.useXTestInsteadOfUinput.value() &&
        xtestBackspaceBackend_.isAvailable()) {
      if (debugEnabled()) {
        FCITX_INFO()
            << "areca: terminal selected native backend (replacing uinput)"
            << " program=" << program
            << " backend=" << xtestBackspaceBackend_.name();
      }
      return {&xtestBackspaceBackend_};
    }
    if (uinputBackspaceBackend_.isAvailable()) {
      if (debugEnabled()) {
        FCITX_INFO() << "areca: terminal selected uinput backend"
                     << " program=" << program
                     << " backend=" << uinputBackspaceBackend_.name();
      }
      return {&uinputBackspaceBackend_};
    }
    if (advancedConfig_.useXTestInsteadOfForwardKey.value() &&
        xtestBackspaceBackend_.isAvailable()) {
      if (debugEnabled()) {
        FCITX_INFO()
            << "areca: terminal selected native backend (replacing forwardKey)"
            << " program=" << program
            << " backend=" << xtestBackspaceBackend_.name();
      }
      return {&xtestBackspaceBackend_};
    }
    if (debugEnabled()) {
      FCITX_INFO()
          << "areca: terminal fallback selected forward-backspace backend"
          << " program=" << program
          << " backend=" << forwardBackspaceBackend_.name();
    }
    return {&forwardBackspaceBackend_};
  }

  if (isPlasmashellProgram(program)) {
    if (debugEnabled()) {
      FCITX_INFO() << "areca: plasmashell forced forward-backspace backend"
                   << " program=" << program
                   << " backend=" << forwardBackspaceBackend_.name();
    }
    return {&forwardBackspaceBackend_};
  }

  if (advancedConfig_.useUinputShiftSelectForDiscordAndSignal.value() &&
      requiresShiftSelectBackend(program) &&
      uinputShiftSelectBackend_.isAvailable()) {
    if (debugEnabled()) {
      FCITX_INFO()
          << "areca: chat compatibility forced uinput-shift-select backend"
          << " program=" << program
          << " backend=" << uinputShiftSelectBackend_.name();
    }
    return {&uinputShiftSelectBackend_};
  }

  if (requiresForwardBackspaceBackend(program)) {
    if (advancedConfig_.useUinputShiftSelectForLibreOffice.value() &&
        uinputShiftSelectBackend_.isAvailable()) {
      if (debugEnabled()) {
        FCITX_INFO()
            << "areca: office compatibility selected uinput-shift-select "
               "backend"
            << " program=" << program
            << " backend=" << uinputShiftSelectBackend_.name();
      }
      return {&uinputShiftSelectBackend_};
    }
    if (advancedConfig_.useXTestInsteadOfUinput.value() &&
        xtestBackspaceBackend_.isAvailable()) {
      if (debugEnabled()) {
        FCITX_INFO() << "areca: office compatibility selected native backend "
                        "(replacing uinput)"
                     << " program=" << program
                     << " backend=" << xtestBackspaceBackend_.name();
      }
      return {&xtestBackspaceBackend_};
    }
    if (debugEnabled()) {
      FCITX_INFO() << "areca: program compatibility selected "
                      "forward-backspace backend"
                   << " program=" << program << " office_shift_select="
                   << advancedConfig_.useUinputShiftSelectForLibreOffice.value()
                   << " backend=" << forwardBackspaceBackend_.name();
    }
    return {&forwardBackspaceBackend_};
  }

  const auto capabilities = inputContext.capabilityFlags();
  const auto &surrounding = inputContext.surroundingText();
  const bool hasSurrounding =
      capabilities.test(fcitx::CapabilityFlag::SurroundingText) &&
      surrounding.isValid();
  // Bắt đầu: Nhận diện thanh địa chỉ Chromium khi SurroundingText không khả
  // dụng hoặc không hợp lệ
  const bool inAddressBar =
      !hasSurrounding && inChromiumAddressBar(inputContext, program, state);
  // Kết thúc: Nhận diện thanh địa chỉ Chromium khi SurroundingText không khả
  // dụng hoặc không hợp lệ
  const bool isUrl =
      capabilities.test(fcitx::CapabilityFlag::Url) || inAddressBar;
  const auto decision =
      evaluateReliability(inputContext, result.currentText, program);
  const bool hasActiveSelection =
      surrounding.isValid() && surrounding.cursor() != surrounding.anchor();
  const bool selectionMatchesCurrentText =
      hasActiveSelection &&
      isSelectionImmediatelyAfterText(surrounding.text(), surrounding.cursor(),
                                      surrounding.anchor(), result.currentText);

  if (decision.browserAutocomplete || hasActiveSelection || inAddressBar) {
    uint32_t additional = 0;
    bool fullReplace = false;
    // Bắt đầu: FullReplace và 1 Backspace phụ cho từ đầu tiên trong thanh địa
    // chỉ
    if (inAddressBar && state && state->addrBarIsFirstWord &&
        !state->addrBarHadSpace) {
      additional = 1;
      fullReplace = true;
    } else if (selectionMatchesCurrentText || decision.browserAutocomplete) {
      additional = 1;
    }
    RewriteBackend *backend = &forwardBackspaceBackend_;
    if (advancedConfig_.useXTestInsteadOfForwardKey.value() &&
        xtestBackspaceBackend_.isAvailable()) {
      backend = &xtestBackspaceBackend_;
    }
    if (debugEnabled()) {
      FCITX_INFO() << "areca: browser autocomplete or address bar strategy="
                   << backend->name() << " is_url=" << isUrl
                   << " in_address_bar=" << inAddressBar
                   << " active_selection=" << hasActiveSelection
                   << " selection_matches_current="
                   << selectionMatchesCurrentText
                   << " full_replace=" << fullReplace
                   << " additional_backspaces=" << additional
                   << " bamboo_delete=" << result.deleteCount;
    }
    return {backend, additional, fullReplace};
  }

  if (!program.empty()) {
    auto it = appBackendOverridesMap_.find(program);
    if (it != appBackendOverridesMap_.end() &&
        it->second != AppBackendMode::Auto) {
      switch (it->second) {
      case AppBackendMode::SurroundingText:
        if (debugEnabled()) {
          FCITX_INFO() << "areca: app-override selected surrounding backend"
                       << " program=" << program;
        }
        return {&surroundingBackend_};
      case AppBackendMode::UinputShiftSelect:
        if (uinputShiftSelectBackend_.isAvailable()) {
          if (debugEnabled()) {
            FCITX_INFO()
                << "areca: app-override selected uinput-shift-select backend"
                << " program=" << program;
          }
          return {&uinputShiftSelectBackend_};
        }
        break;
      case AppBackendMode::NativeXTest:
        if (xtestBackspaceBackend_.isAvailable()) {
          if (debugEnabled()) {
            FCITX_INFO() << "areca: app-override selected native/xtest backend"
                         << " program=" << program;
          }
          return {&xtestBackspaceBackend_};
        }
        if (uinputBackspaceBackend_.isAvailable()) {
          if (debugEnabled()) {
            FCITX_INFO() << "areca: app-override native/xtest unavailable, "
                            "fallback to uinput backend"
                         << " program=" << program;
          }
          return {&uinputBackspaceBackend_};
        }
        break;
      case AppBackendMode::UinputBackspace:
        if (uinputBackspaceBackend_.isAvailable()) {
          if (debugEnabled()) {
            FCITX_INFO()
                << "areca: app-override selected uinput-backspace backend"
                << " program=" << program;
          }
          return {&uinputBackspaceBackend_};
        }
        break;
      case AppBackendMode::ForwardKey:
        if (debugEnabled()) {
          FCITX_INFO() << "areca: app-override selected forward-key backend"
                       << " program=" << program;
        }
        return {&forwardBackspaceBackend_};
      case AppBackendMode::Auto:
        break;
      }
    }
  }

  const bool isBrowserForShiftSelect =
      !program.empty() && !isTerminal && inputTypeDetector_.isBrowser(program);

  if (decision.useSurrounding) {
    if (advancedConfig_.useUinputShiftSelectForSurrounding.value() &&
        uinputShiftSelectBackend_.isAvailable()) {
      if (debugEnabled()) {
        FCITX_INFO() << "areca: forced uinput-shift-select backend instead of "
                        "surrounding"
                     << " program=" << program
                     << " frontend=" << (frontend ? frontend : "")
                     << " backend=" << uinputShiftSelectBackend_.name();
      }
      return {&uinputShiftSelectBackend_};
    }
    if (advancedConfig_.useUinputShiftSelectForBrowser.value() &&
        isBrowserForShiftSelect && uinputShiftSelectBackend_.isAvailable()) {
      if (debugEnabled()) {
        FCITX_INFO()
            << "areca: selected uinput-shift-select backend for browser"
            << " program=" << program
            << " frontend=" << (frontend ? frontend : "")
            << " backend=" << uinputShiftSelectBackend_.name();
      }
      return {&uinputShiftSelectBackend_};
    }
    if (advancedConfig_.useSurroundingV2ForBrowser.value() &&
        isBrowserForShiftSelect) {
      if (debugEnabled()) {
        FCITX_INFO() << "areca: selected surrounding-text-v2 backend"
                     << " program=" << program
                     << " frontend=" << (frontend ? frontend : "")
                     << " backend=" << surroundingV2Backend_.name();
      }
      return {&surroundingV2Backend_};
    }
    return {&surroundingBackend_};
  }

  if (advancedConfig_.useUinputShiftSelectForBrowser.value() &&
      isBrowserForShiftSelect && uinputShiftSelectBackend_.isAvailable()) {
    if (debugEnabled()) {
      FCITX_INFO() << "areca: selected uinput-shift-select backend for browser"
                   << " program=" << program
                   << " frontend=" << (frontend ? frontend : "")
                   << " backend=" << uinputShiftSelectBackend_.name();
    }
    return {&uinputShiftSelectBackend_};
  }

  // Fallback: nếu browser không hỗ trợ surrounding text thì dùng shift-select
  // thay vì forward-backspace. Khác với useUinputShiftSelectForBrowser vốn
  // override cả nhánh useSurrounding bên trên.
  if (config_.shiftSelectFallbackForBrowser.value() &&
      isBrowserForShiftSelect && uinputShiftSelectBackend_.isAvailable()) {
    if (debugEnabled()) {
      FCITX_INFO()
          << "areca: browser no surrounding text, fallback to shift-select"
          << " program=" << program
          << " frontend=" << (frontend ? frontend : "")
          << " backend=" << uinputShiftSelectBackend_.name();
    }
    return {&uinputShiftSelectBackend_};
  }

  if (advancedConfig_.forceUinput.value()) {
    if (advancedConfig_.useXTestInsteadOfUinput.value() &&
        xtestBackspaceBackend_.isAvailable()) {
      if (debugEnabled()) {
        FCITX_INFO() << "areca: selected native backend (replacing uinput)"
                     << " program=" << program
                     << " backend=" << xtestBackspaceBackend_.name();
      }
      return {&xtestBackspaceBackend_};
    }
    if (uinputBackspaceBackend_.isAvailable()) {
      if (debugEnabled()) {
        FCITX_INFO()
            << "areca: forced uinput backend for forward backspace fallback"
            << " program=" << program
            << " backend=" << uinputBackspaceBackend_.name();
      }
      return {&uinputBackspaceBackend_};
    }
  }

  if (advancedConfig_.useXTestInsteadOfForwardKey.value() &&
      xtestBackspaceBackend_.isAvailable()) {
    if (debugEnabled()) {
      FCITX_INFO() << "areca: selected native backend (replacing forwardKey)"
                   << " program=" << program
                   << " backend=" << xtestBackspaceBackend_.name();
    }
    return {&xtestBackspaceBackend_};
  }

  return {&forwardBackspaceBackend_};
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

  if (!backendVerdict_.known && !browserAutocomplete) {
    backendVerdict_.reliable = !isInvalidSurrounding(surrounding);
    if (debugEnabled()) {
      FCITX_INFO() << "areca: reliability first-probe text="
                   << surrounding.text() << " cursor=" << surrounding.cursor()
                   << " anchor=" << surrounding.anchor()
                   << " reliable=" << backendVerdict_.reliable;
    }

    backendVerdict_.forceForwardBackspace = false;
    const bool atspiInactive = !focusTracker_ || !focusTracker_->isValid();
    if (atspiInactive && backendVerdict_.reliable &&
        isVSCodeFamilyProgram(programName)) {
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

void ArecaEngine::scheduleDeviceWarmup() {
  deviceWarmupTimer_.reset();
  bool warmUpNative = advancedConfig_.useXTestInsteadOfUinput.value() ||
                      advancedConfig_.useXTestInsteadOfForwardKey.value();
  if (!warmUpNative) {
    for (const auto &pair : appBackendOverridesMap_) {
      if (pair.second == AppBackendMode::NativeXTest) {
        warmUpNative = true;
        break;
      }
    }
  }
  const uint64_t deadline = fcitx::now(CLOCK_MONOTONIC);
  deviceWarmupTimer_ = instance_->eventLoop().addTimeEvent(
      CLOCK_MONOTONIC, deadline, 0,
      [this, warmUpNative](fcitx::EventSourceTime *, uint64_t) {
        auto timer = std::move(deviceWarmupTimer_);
        const bool uinputAvailable = uinputDevice_.ensureDevice();
        const bool nativeStarted = warmUpNative && nativeDevice_.warmUp();
        if (debugEnabled()) {
          FCITX_INFO() << "areca: device warmup completed"
                       << " uinput-available=" << uinputAvailable
                       << " native-requested=" << warmUpNative
                       << " native-started=" << nativeStarted;
        }
        return false;
      });
  if (deviceWarmupTimer_) {
    deviceWarmupTimer_->setOneShot();
  }
}

InputModeHandler &ArecaEngine::activeHandler() {
  switch (activePresentationMode_) {
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
                                 fcitx::InputContext &) {
  return presentationModeName(activePresentationMode_);
}

std::string ArecaEngine::subModeIconImpl(const fcitx::InputMethodEntry &,
                                         fcitx::InputContext &) {
  return "org.fcitx.Fcitx5.fcitx-areca";
}

std::string ArecaEngine::subModeLabelImpl(const fcitx::InputMethodEntry &,
                                          fcitx::InputContext &) {
  return "Ă  " + config_.bambooInputMethod.value() + " \xC2\xB7 " +
         presentationModeName(activePresentationMode_);
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
  activeHandler().activate(inputContext);
  save();

  if (debugEnabled()) {
    FCITX_INFO() << "areca: switch mode hotkey mode="
                 << presentationModeName(next)
                 << " program=" << inputContext.program();
  }
  instance_->showInputMethodInformation(&inputContext);
}

bool ArecaEngine::isBackendSelecting(fcitx::InputContext &inputContext) const {
  auto *candList = inputContext.inputPanel().candidateList().get();
  return candList &&
         dynamic_cast<fcitx::CommonCandidateList *>(candList) != nullptr &&
         !inputContext.inputPanel().auxUp().toString().empty();
}

void ArecaEngine::showBackendSelectionMenu(fcitx::InputContext &inputContext) {
  auto *rewriteState = inputContext.propertyFor(&rewriteStateFactory_);
  const std::string appName = resolveProgram(inputContext, rewriteState);
  if (appName.empty()) {
    return;
  }

  auto currentIt = appBackendOverridesMap_.find(appName);
  const AppBackendMode currentMode =
      (currentIt != appBackendOverridesMap_.end()) ? currentIt->second
                                                   : AppBackendMode::Auto;

  auto candList = std::make_unique<fcitx::CommonCandidateList>();
  candList->setLayoutHint(fcitx::CandidateLayoutHint::Vertical);
  candList->setLabels({"1. ", "2. ", "3. ", "4. ", "5. ", "6. "});
  candList->setPageSize(6);

  struct BackendOption {
    AppBackendMode mode;
    std::string title;
  };
  const std::array<BackendOption, 6> options = {{
      {AppBackendMode::Auto, "Tự động"},
      {AppBackendMode::SurroundingText, "Surrounding Text"},
      {AppBackendMode::UinputShiftSelect, "Shift+Left (uinput)"},
      {AppBackendMode::NativeXTest, "Native (Libei/XTest)"},
      {AppBackendMode::UinputBackspace, "Uinput Backspace"},
      {AppBackendMode::ForwardKey, "ForwardKey"},
  }};

  int initialCursor = 0;
  for (size_t i = 0; i < options.size(); ++i) {
    std::string label = options[i].title;
    if (options[i].mode == currentMode) {
      label += "  [Hiện tại]";
      initialCursor = static_cast<int>(i);
    }
    candList->append(std::make_unique<BackendCandidateWord>(
        fcitx::Text(label), options[i].mode,
        [this, &inputContext, appName](AppBackendMode mode) {
          selectBackendForApp(inputContext, appName, mode);
        }));
  }

  candList->setGlobalCursorIndex(initialCursor);

  auto &panel = inputContext.inputPanel();
  panel.reset();
  panel.setAuxUp(fcitx::Text("Chọn backend cho: " + appName));
  panel.setCandidateList(std::move(candList));
  inputContext.updateUserInterface(fcitx::UserInterfaceComponent::InputPanel,
                                   true);
}

void ArecaEngine::selectBackendForApp(fcitx::InputContext &inputContext,
                                      const std::string &appName,
                                      AppBackendMode mode) {
  auto &overrides = *appProfiles_.appOverrides.mutableValue();
  auto it = std::find_if(overrides.begin(), overrides.end(),
                         [&appName](const AppBackendOverrideEntry &entry) {
                           return entry.appName.value() == appName;
                         });

  if (it != overrides.end()) {
    it->mode.setValue(mode);
  } else {
    AppBackendOverrideEntry entry;
    entry.appName.setValue(appName);
    entry.mode.setValue(mode);
    overrides.push_back(std::move(entry));
  }

  applyConfig();
  save();

  inputContext.inputPanel().reset();
  inputContext.updateUserInterface(fcitx::UserInterfaceComponent::InputPanel,
                                   true);

  std::string modeDesc;
  switch (mode) {
  case AppBackendMode::Auto:
    modeDesc = "Tự động";
    break;
  case AppBackendMode::SurroundingText:
    modeDesc = "Surrounding Text";
    break;
  case AppBackendMode::UinputShiftSelect:
    modeDesc = "Shift+Left (uinput)";
    break;
  case AppBackendMode::NativeXTest:
    modeDesc = "Native (Libei/XTest)";
    break;
  case AppBackendMode::UinputBackspace:
    modeDesc = "Uinput Backspace";
    break;
  case AppBackendMode::ForwardKey:
    modeDesc = "ForwardKey";
    break;
  }

  const std::string infoMsg = appName + " -> " + modeDesc;
#ifdef ARECA_HAVE_SHOW_CUSTOM_INFO
  instance_->showCustomInputMethodInformation(&inputContext, infoMsg);
#else
  instance_->showInputMethodInformation(&inputContext);
#endif
}

bool ArecaEngine::handleBackendSelectionKey(fcitx::InputContext &inputContext,
                                            fcitx::KeyEvent &event) {
  if (event.isRelease()) {
    return true;
  }

  auto *candList = inputContext.inputPanel().candidateList().get();
  auto *commonList = dynamic_cast<fcitx::CommonCandidateList *>(candList);
  if (!commonList) {
    return false;
  }

  const auto key = event.key().normalize();
  const auto sym = key.sym();

  if (key.checkKeyList(config_.selectBackendKey.value())) {
    inputContext.inputPanel().reset();
    inputContext.updateUserInterface(fcitx::UserInterfaceComponent::InputPanel,
                                     true);
    return false;
  }

  if (sym == FcitxKey_Escape) {
    inputContext.inputPanel().reset();
    inputContext.updateUserInterface(fcitx::UserInterfaceComponent::InputPanel,
                                     true);
    return true;
  }

  if (sym >= FcitxKey_1 && sym <= FcitxKey_6) {
    const int idx = sym - FcitxKey_1;
    if (idx < commonList->size()) {
      commonList->candidate(idx).select(&inputContext);
    }
    return true;
  }

  if (sym == FcitxKey_Up) {
    commonList->prevCandidate();
    inputContext.updateUserInterface(fcitx::UserInterfaceComponent::InputPanel,
                                     true);
    return true;
  }

  if (sym == FcitxKey_Down) {
    commonList->nextCandidate();
    inputContext.updateUserInterface(fcitx::UserInterfaceComponent::InputPanel,
                                     true);
    return true;
  }

  if (sym == FcitxKey_Return || sym == FcitxKey_KP_Enter ||
      sym == FcitxKey_space) {
    const int curIdx = commonList->cursorIndex();
    if (curIdx >= 0 && curIdx < commonList->size()) {
      commonList->candidate(curIdx).select(&inputContext);
    }
    return true;
  }

  // Chặn mọi phím khác khi menu đang mở
  return true;
}

void ArecaEngine::activate(const fcitx::InputMethodEntry &,
                           fcitx::InputContextEvent &event) {
  auto *inputContext = event.inputContext();
  if (!inputContext) {
    return;
  }
  if (activePresentationMode_ == PresentationMode::Rewrite) {
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
  activeHandler().activate(*inputContext);
  if (debugEnabled()) {
    FCITX_INFO() << "areca: activate presentation_mode="
                 << presentationModeName(activePresentationMode_)
                 << " program=" << inputContext->program();
  }
  auto &statusArea = inputContext->statusArea();
  statusArea.addAction(fcitx::StatusGroup::InputMethod, settingsAction_.get());
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
    activeHandler().handleKeyEvent(event);
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
  // Uinput-backspace dùng Backspace dư làm tín hiệu xác nhận. Backend phải
  // nhận event trước RewriteMode để filter đúng phím dư và giữ nguyên state.
  if (uinputBackspaceBackend_.handleBackspace(event)) {
    return;
  }
  // Shift-select tự đếm Left uinput quay lại. N Left đầu được forward, Left dư
  // thứ N+1 bị filter và là tín hiệu an toàn để backend thả Shift.
  if (uinputShiftSelectBackend_.handleSelectionLeft(event)) {
    return;
  }
  if (!event.isRelease()) {
    // Resolve program một lần để nhận diện address bar dùng cùng verdict trong
    // toàn bộ lượt xử lý phím.
    auto *state = inputContext->propertyFor(&rewriteStateFactory_);
    const std::string program = resolveProgram(*inputContext, state);

    // Reset trước phím nhấn tiếp theo vì click có thể đã đổi vị trí con trỏ.
    // Nếu rewrite còn được bảo vệ, giữ cờ click cho lần sau thay vì làm mất nó
    // hoặc xóa trạng thái giữa một chuỗi thao tác xóa/chèn đang chạy.
    if (mouseTracker_ && mouseTracker_->hasPendingClick()) {
      if (scheduler_.shouldRejectReset()) {
        if (debugEnabled())
          FCITX_INFO() << "areca: mouse reset deferred (rewrite protection)";
      } else {
        if (debugEnabled())
          FCITX_INFO() << "areca: applying mouse reset mode="
                       << presentationModeName(activePresentationMode_);
        activeHandler().resetContext(*inputContext);
        clearBackendVerdictForLifecycle(*inputContext, "mouse-click");
        mouseTracker_->clearPendingClick();
      }
    }
    // Bắt đầu: Làm nóng trạng thái nhận diện thanh địa chỉ và cửa sổ trễ ngay
    // từ ký tự đầu tiên
    inChromiumAddressBar(*inputContext, program, state);
    // Kết thúc: Làm nóng trạng thái nhận diện thanh địa chỉ và cửa sổ trễ ngay
    // từ ký tự đầu tiên

    const auto key = event.key().normalize();
    if (key.sym() != FcitxKey_None &&
        key.checkKeyList(config_.switchModeKey.value())) {
      if (inputContext->capabilityFlags().test(
              fcitx::CapabilityFlag::Password)) {
        activeHandler().handleKeyEvent(event);
        return;
      }
      event.filterAndAccept();
      switchPresentationMode(*inputContext);
      return;
    }

    if (key.sym() != FcitxKey_None &&
        key.checkKeyList(config_.selectBackendKey.value())) {
      if (inputContext->capabilityFlags().test(
              fcitx::CapabilityFlag::Password)) {
        activeHandler().handleKeyEvent(event);
        return;
      }
      bool isComposing = false;
      if (activePresentationMode_ == PresentationMode::Rewrite) {
        if (state && state->engine && !state->engine->currentText().empty()) {
          isComposing = true;
        }
      } else if (activePresentationMode_ == PresentationMode::Preedit) {
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
  activeHandler().handleKeyEvent(event);
}

void ArecaEngine::deactivate(const fcitx::InputMethodEntry &,
                             fcitx::InputContextEvent &event) {
  auto *inputContext = event.inputContext();
  if (!inputContext) {
    return;
  }
  if (activePresentationMode_ == PresentationMode::Rewrite) {
    clearBackendVerdictForLifecycle(*inputContext, "deactivate");
  }
  activeHandler().deactivate(*inputContext);
}

void ArecaEngine::reset(const fcitx::InputMethodEntry &,
                        fcitx::InputContextEvent &event) {
  if (auto *inputContext = event.inputContext()) {
    if (scheduler_.shouldRejectReset()) {
      if (debugEnabled()) {
        FCITX_INFO() << "areca: app reset rejected (active rewrite or 20ms "
                        "post-commit window)";
      }
      return;
    }
    if (activePresentationMode_ == PresentationMode::Rewrite) {
      clearBackendVerdictForLifecycle(*inputContext, "reset");
    }
    if (debugEnabled()) {
      FCITX_INFO() << "areca: app reset requested; arm protected reset";
    }
    activeHandler().requestProtectedReset(*inputContext);
  }
}

const fcitx::Configuration *ArecaEngine::getConfig() const { return &config_; }

const fcitx::Configuration *
ArecaEngine::getSubConfig(const std::string &path) const {
  if (path == "macro") {
    return &macroTable_;
  }
  if (path == "apps") {
    return &appProfiles_;
  }
  if (path == "advanced") {
    return &advancedConfig_;
  }
  return nullptr;
}

void ArecaEngine::setConfig(const fcitx::RawConfig &config) {
  config_.load(config, true);
  applyConfig();
  save();
}

void ArecaEngine::setSubConfig(const std::string &path,
                               const fcitx::RawConfig &config) {
  if (path == "advanced") {
    advancedConfig_.load(config, true);
    fcitx::safeSaveAsIni(advancedConfig_, kPkgConfigPath, kAdvancedConfigPath);
    applyConfig();
    return;
  }
  if (path == "apps") {
    appProfiles_.load(config, true);
    fcitx::safeSaveAsIni(appProfiles_, kPkgConfigPath, kAppsConfigPath);
    applyConfig();
    return;
  }
  if (path != "macro") {
    return;
  }
  macroTable_.load(config, true);
  fcitx::safeSaveAsIni(macroTable_, kPkgConfigPath, kMacroConfigPath);
  ++macroRevision_;
  applyConfig();
}

void ArecaEngine::reloadConfig() {
  fcitx::readAsIni(config_, kPkgConfigPath, "conf/areca.conf");
  // Seed the advanced panel from legacy timing fields before loading its own
  // file. Once the advanced file exists, its values take precedence.
  advancedConfig_.backspaceDelayMs.setValue(
      config_.legacyBackspaceDelayMs.value());
  advancedConfig_.afterBackspaceWaitMs.setValue(
      config_.legacyAfterBackspaceWaitMs.value());
  advancedConfig_.postCommitDelayMs.setValue(
      config_.legacyPostCommitDelayMs.value());
  fcitx::readAsIni(advancedConfig_, kPkgConfigPath, kAdvancedConfigPath);
  fcitx::readAsIni(macroTable_, kPkgConfigPath, kMacroConfigPath);
  fcitx::readAsIni(appProfiles_, kPkgConfigPath, kAppsConfigPath);
  ++macroRevision_;
  applyConfig();
}

void ArecaEngine::save() {
  fcitx::safeSaveAsIni(config_, kPkgConfigPath, "conf/areca.conf");
  fcitx::safeSaveAsIni(macroTable_, kPkgConfigPath, kMacroConfigPath);
  fcitx::safeSaveAsIni(appProfiles_, kPkgConfigPath, kAppsConfigPath);
  fcitx::safeSaveAsIni(advancedConfig_, kPkgConfigPath, kAdvancedConfigPath);
}

std::vector<MacroDefinition> ArecaEngine::macroDefinitions() const {
  std::vector<MacroDefinition> result;
  const auto &entries = macroTable_.macros.value();
  result.reserve(entries.size());
  for (const auto &entry : entries) {
    if (!entry.key.value().empty() && !entry.value.value().empty()) {
      result.push_back({entry.key.value(), entry.value.value()});
    }
  }
  return result;
}

SchedulerTiming ArecaEngine::timing() const {
  return {
      static_cast<uint32_t>(advancedConfig_.backspaceDelayMs.value()),
      static_cast<uint32_t>(advancedConfig_.afterBackspaceWaitMs.value()),
      static_cast<uint32_t>(
          advancedConfig_.waylandAfterBackspaceWaitMs.value()),
      static_cast<uint32_t>(advancedConfig_.ximAfterBackspaceWaitMs.value()),
      static_cast<uint32_t>(advancedConfig_.fcitx4AfterBackspaceWaitMs.value()),
      static_cast<uint32_t>(advancedConfig_.dbusAfterBackspaceWaitMs.value()),
      static_cast<uint32_t>(advancedConfig_.uinputShiftSelectDelayMs.value()),
      static_cast<uint32_t>(
          advancedConfig_.afterUinputShiftSelectWaitMs.value()),
      static_cast<uint32_t>(
          advancedConfig_.waylandAfterUinputShiftSelectWaitMs.value()),
      static_cast<uint32_t>(
          advancedConfig_.ximAfterUinputShiftSelectWaitMs.value()),
      static_cast<uint32_t>(
          advancedConfig_.fcitx4AfterUinputShiftSelectWaitMs.value()),
      static_cast<uint32_t>(
          advancedConfig_.dbusAfterUinputShiftSelectWaitMs.value()),
      static_cast<uint32_t>(advancedConfig_.surroundingWaitMs.value()),
      static_cast<uint32_t>(advancedConfig_.surroundingDeleteDelayMs.value()),
      static_cast<uint32_t>(
          advancedConfig_.waylandSurroundingDeleteDelayMs.value()),
      static_cast<uint32_t>(
          advancedConfig_.afterSurroundingDeleteWaitMs.value()),
      static_cast<uint32_t>(advancedConfig_.xtestBackspaceDelayMs.value()),
      static_cast<uint32_t>(advancedConfig_.afterXTestBackspaceWaitMs.value()),
      static_cast<uint32_t>(
          advancedConfig_.waylandAfterXTestBackspaceWaitMs.value()),
      static_cast<uint32_t>(
          advancedConfig_.ximAfterXTestBackspaceWaitMs.value()),
      static_cast<uint32_t>(
          advancedConfig_.fcitx4AfterXTestBackspaceWaitMs.value()),
      static_cast<uint32_t>(
          advancedConfig_.dbusAfterXTestBackspaceWaitMs.value()),
      static_cast<uint32_t>(advancedConfig_.postCommitDelayMs.value()),
      advancedConfig_.preciseTiming.value() ? 1U : 0U};
}

void ArecaEngine::rebuildAppBackendOverridesMap() {
  appBackendOverridesMap_.clear();
  for (const auto &entry : appProfiles_.appOverrides.value()) {
    const std::string &appName = entry.appName.value();
    if (!appName.empty()) {
      appBackendOverridesMap_[appName] = entry.mode.value();
    }
  }
}

void ArecaEngine::applyConfig() {
  rebuildAppBackendOverridesMap();
  // Start the Native transport before the first rewrite whenever either
  // legacy XTest policy key is enabled. Wayland requests Libei permission;
  // XTest stays closed unless Libei fails. On X11, Native uses XTest directly.
  scheduleDeviceWarmup();

  // Tắt là hủy cả watcher, pipe, cờ click và process con; không chỉ bỏ qua
  // reset. Khi bật lại, tạo tracker mới để không xử lý click tồn đọng từ trước.
  if (advancedConfig_.enableMouseTracking.value()) {
    if (!mouseTracker_) {
      mouseTracker_ = std::make_unique<MouseClickTracker>(
          instance_->eventLoop(), [this]() { return debugEnabled(); });
      if (!mouseTracker_->start())
        FCITX_WARN() << "areca: mouse monitor unavailable";
    }
  } else {
    mouseTracker_.reset();
  }
  if (debugEnabled())
    FCITX_INFO() << "areca: input options mouse_tracking="
                 << advancedConfig_.enableMouseTracking.value()
                 << " forward_first_character="
                 << advancedConfig_.forwardFirstCharacter.value();

  if (scheduler_.rewritePending()) {
    return;
  }

  const auto inputMethod = config_.bambooInputMethod.value();
  const auto spellcheckMode = config_.spellcheckMode.value();
  const bool spellCheck = spellcheckMode != SpellcheckMode::Off;
  const bool realtimeSpellcheck = spellcheckMode == SpellcheckMode::Realtime;
  const bool modernStyle = config_.modernStyle.value();
  const auto outputCharset = config_.outputCharset.value();
  const bool macroEnabled = config_.enableMacro.value();
  const bool capitalizeMacro = config_.capitalizeMacro.value();
  const bool autoCapitalize = config_.autoCapitalizeAfterPunctuation.value();
  const auto requestedMode = config_.presentationMode.value();
  const bool modeChanged = requestedMode != activePresentationMode_;
  const auto macros = macroDefinitions();
  instance_->inputContextManager().foreach (
      [this, &inputMethod, spellCheck, realtimeSpellcheck, modernStyle,
       &outputCharset, macroEnabled, capitalizeMacro, autoCapitalize,
       modeChanged, &macros](fcitx::InputContext *inputContext) {
        if (modeChanged) {
          rewriteHandler_.resetContext(*inputContext);
          preeditHandler_.resetContext(*inputContext);
        }

        auto updateEngine = [&](auto *state, auto resetMode) {
          if (!state) {
            return;
          }
          if (!autoCapitalize) {
            state->sentenceCapitalization.reset();
          }
          if (state->inputMethod == inputMethod &&
              state->spellCheck == spellCheck &&
              state->realtimeSpellcheck == realtimeSpellcheck &&
              state->modernStyle == modernStyle &&
              state->outputCharset == outputCharset &&
              state->macroEnabled == macroEnabled &&
              state->capitalizeMacro == capitalizeMacro &&
              state->macroRevision == macroRevision_) {
            return;
          }
          try {
            resetMode();
            state->engine = std::make_unique<BambooEngineAdapter>(
                inputMethod, spellCheck, realtimeSpellcheck, modernStyle,
                outputCharset, macroEnabled, capitalizeMacro, macros);
            state->inputMethod = inputMethod;
            state->spellCheck = spellCheck;
            state->realtimeSpellcheck = realtimeSpellcheck;
            state->modernStyle = modernStyle;
            state->outputCharset = outputCharset;
            state->macroEnabled = macroEnabled;
            state->capitalizeMacro = capitalizeMacro;
            state->macroRevision = macroRevision_;
          } catch (const std::exception &error) {
            FCITX_ERROR() << "areca: cannot select Bamboo method: "
                          << error.what();
          }
        };

        updateEngine(rewriteHandler_.stateFor(*inputContext),
                     [this, inputContext]() {
                       rewriteHandler_.resetContext(*inputContext);
                     });
        updateEngine(preeditHandler_.stateFor(*inputContext),
                     [this, inputContext]() {
                       preeditHandler_.resetContext(*inputContext);
                     });
        return true;
      });
  activePresentationMode_ = requestedMode;
}

class ArecaEngineFactory final : public fcitx::AddonFactory {
public:
  fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
    return new ArecaEngine(manager->instance());
  }
};

} // namespace areca

FCITX_ADDON_FACTORY(areca::ArecaEngineFactory)
