#include "areca_engine.h"

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
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>

#include "browser_autocomplete.h"
#include "program_compatibility.h"

namespace areca {
namespace {

constexpr const char *kMacroConfigPath = "conf/areca-macro-table.conf";
constexpr const char *kAdvancedConfigPath = "conf/areca-advanced.conf";
constexpr uint64_t kBackendVerdictProtectionUsec = 1ULL * 1000 * 1000;
#if defined(ARECA_HAS_STANDARD_PATHS)
constexpr auto kPkgConfigPath = fcitx::StandardPathsType::PkgConfig;
#else
constexpr auto kPkgConfigPath = fcitx::StandardPath::Type::PkgConfig;
#endif

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
      surroundingBackend_(instance_->eventLoop(),
                          [this]() { return debugEnabled(); }),
      forwardBackspaceBackend_(instance_->eventLoop(),
                               [this]() { return debugEnabled(); }),
      uinputBackspaceBackend_(instance_->eventLoop(),
                              [this]() { return debugEnabled(); }),
      uinputShiftSelectBackend_(instance_->eventLoop(),
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
          [this]() { return backspaceRecoveryEnabled(); }),
      preeditHandler_(
          instance_->eventLoop(), preeditStateFactory_,
          [this]() { return debugEnabled(); },
          [this]() { return config_.autoCapitalizeAfterPunctuation.value(); }) {
  instance_->inputContextManager().registerProperty("arecaRewriteState",
                                                    &rewriteStateFactory_);
  instance_->inputContextManager().registerProperty("arecaPreeditState",
                                                    &preeditStateFactory_);
  config_.bambooInputMethod.annotation().setList(
      BambooEngineAdapter::inputMethodNames());
  config_.outputCharset.annotation().setList(
      BambooEngineAdapter::charsetNames());
  reloadConfig();
  scheduleUinputWarmup();
}

ArecaEngine::~ArecaEngine() = default;

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

RewriteBackendSelection
ArecaEngine::selectRewriteBackend(fcitx::InputContext &inputContext,
                                  const BambooResult &result) {
  auto *state = inputContext.propertyFor(&rewriteStateFactory_);
  if (!state) {
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

  const auto capabilities = inputContext.capabilityFlags();
  const auto decision = reliabilityChecker_.evaluate(
      inputContext, result.currentText, backendVerdict_, debugEnabled());
  if (decision.browserAutocomplete) {
    const bool isUrl = capabilities.test(fcitx::CapabilityFlag::Url);
    if (debugEnabled()) {
      FCITX_INFO() << "areca: browser autocomplete strategy="
                   << forwardBackspaceBackend_.name()
                   << " is_url=" << isUrl
                   << " additional_backspaces=1"
                   << " bamboo_delete=" << result.deleteCount;
    }
    return {&forwardBackspaceBackend_, 1};
  }

  if (decision.useSurrounding) {
    return {&surroundingBackend_};
  }

  const char *frontend = inputContext.frontend();
  const std::string &program = inputContext.program();

  if ((isBrowserLikeProgram(program) ||
       (frontend && std::string_view(frontend) == "xim")) &&
      uinputShiftSelectBackend_.isAvailable()) {
    if (debugEnabled()) {
      FCITX_INFO() << "areca: selected uinput-shift-select backend for browser or XIM"
                   << " program=" << program
                   << " frontend=" << (frontend ? frontend : "")
                   << " backend=" << uinputShiftSelectBackend_.name();
    }
    return {&uinputShiftSelectBackend_};
  }

  if (frontend && std::string_view(frontend) == "dbus" &&
      (program.empty() || isTerminalProgram(program))) {
    if (uinputBackspaceBackend_.isAvailable()) {
      if (debugEnabled()) {
        FCITX_INFO() << "areca: DBus terminal/unknown program selected uinput backend"
                     << " program=" << program
                     << " backend=" << uinputBackspaceBackend_.name();
      }
      return {&uinputBackspaceBackend_};
    }
  }

  if (advancedConfig_.forceUinput.value() &&
      uinputBackspaceBackend_.isAvailable()) {
    if (debugEnabled()) {
      FCITX_INFO() << "areca: forced uinput backend for forward backspace fallback"
                   << " program=" << program
                   << " backend=" << uinputBackspaceBackend_.name();
    }
    return {&uinputBackspaceBackend_};
  }

  return {&forwardBackspaceBackend_};
}

bool ArecaEngine::backspaceRecoveryEnabled() const {
  return advancedConfig_.backspaceRecovery.value();
}

void ArecaEngine::scheduleUinputWarmup() {
  uinputWarmupTimer_.reset();
  const uint64_t deadline = fcitx::now(CLOCK_MONOTONIC);
  uinputWarmupTimer_ = instance_->eventLoop().addTimeEvent(
      CLOCK_MONOTONIC, deadline, 0,
      [this](fcitx::EventSourceTime *, uint64_t) {
        auto timer = std::move(uinputWarmupTimer_);
        const bool backspaceAvailable = uinputBackspaceBackend_.isAvailable();
        const bool shiftSelectAvailable =
            uinputShiftSelectBackend_.isAvailable();
        if (debugEnabled()) {
          FCITX_INFO() << "areca: uinput warmup completed"
                       << " uinput-backspace=" << backspaceAvailable
                       << " uinput-shift-select=" << shiftSelectAvailable;
        }
        return false;
      });
  if (uinputWarmupTimer_) {
    uinputWarmupTimer_->setOneShot();
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

void ArecaEngine::activate(const fcitx::InputMethodEntry &,
                           fcitx::InputContextEvent &event) {
  auto *inputContext = event.inputContext();
  if (!inputContext) {
    return;
  }
  if (activePresentationMode_ == PresentationMode::Rewrite) {
    clearBackendVerdictForLifecycle(*inputContext, "activate");
  }
  activeHandler().activate(*inputContext);
  if (debugEnabled()) {
    FCITX_INFO() << "areca: activate presentation_mode="
                 << presentationModeName(activePresentationMode_)
                 << " program=" << inputContext->program();
  }
}

void ArecaEngine::keyEvent(const fcitx::InputMethodEntry &,
                           fcitx::KeyEvent &event) {
  auto *inputContext = event.inputContext();
  if (!inputContext) {
    return;
  }
  if (!event.isRelease()) {
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
        FCITX_INFO() << "areca: app reset rejected (active rewrite or 300ms "
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
  ++macroRevision_;
  applyConfig();
}

void ArecaEngine::save() {
  fcitx::safeSaveAsIni(config_, kPkgConfigPath, "conf/areca.conf");
  fcitx::safeSaveAsIni(macroTable_, kPkgConfigPath, kMacroConfigPath);
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
  return {static_cast<uint32_t>(advancedConfig_.backspaceDelayMs.value()),
          static_cast<uint32_t>(advancedConfig_.afterBackspaceWaitMs.value()),
          static_cast<uint32_t>(
              advancedConfig_.waylandAfterBackspaceWaitMs.value()),
          static_cast<uint32_t>(
              advancedConfig_.ximAfterBackspaceWaitMs.value()),
          static_cast<uint32_t>(
              advancedConfig_.fcitx4AfterBackspaceWaitMs.value()),
          static_cast<uint32_t>(
              advancedConfig_.dbusAfterBackspaceWaitMs.value()),
          static_cast<uint32_t>(advancedConfig_.postCommitDelayMs.value()),
          advancedConfig_.preciseTiming.value() ? 1U : 0U};
}

void ArecaEngine::applyConfig() {
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
