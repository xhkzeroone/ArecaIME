#include "areca_engine.h"

#include <fcitx-config/iniparser.h>
#include <fcitx-utils/log.h>
#if __has_include(<fcitx-utils/standardpaths.h>)
#include <fcitx-utils/standardpaths.h>
#define ARECA_HAS_STANDARD_PATHS 1
#else
#include <fcitx-utils/standardpath.h>
#endif
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>

#include "mouse_click_tracker.h"
#include "window_focus_tracker.h"

namespace areca {

namespace {

constexpr const char *kMacroConfigPath = "conf/areca-macro-table.conf";
constexpr const char *kAppsConfigPath = "conf/areca-apps.conf";
constexpr const char *kAdvancedConfigPath = "conf/areca-advanced.conf";
#if defined(ARECA_HAS_STANDARD_PATHS)
constexpr auto kPkgConfigPath = fcitx::StandardPathsType::PkgConfig;
#else
constexpr auto kPkgConfigPath = fcitx::StandardPath::Type::PkgConfig;
#endif

} // namespace

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
  if (inputMethodAction_) {
    updateUI(nullptr);
  }
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
  if (inputMethodAction_) {
    updateUI(nullptr);
  }
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

SchedulerTiming ArecaEngine::timing(fcitx::InputContext &) {
  return SchedulerTiming{
      static_cast<uint32_t>(advancedConfig_.backspaceDelayMs.value()),
      static_cast<uint32_t>(
          advancedConfig_.uinputBackspaceAckTimeoutMs.value()),
      static_cast<uint32_t>(advancedConfig_.afterBackspaceWaitMs.value()),
      static_cast<uint32_t>(
          advancedConfig_.waylandAfterBackspaceWaitMs.value()),
      static_cast<uint32_t>(advancedConfig_.ximAfterBackspaceWaitMs.value()),
      static_cast<uint32_t>(advancedConfig_.fcitx4AfterBackspaceWaitMs.value()),
      static_cast<uint32_t>(advancedConfig_.dbusAfterBackspaceWaitMs.value()),
      static_cast<uint32_t>(advancedConfig_.ibusAfterBackspaceWaitMs.value()),
      static_cast<uint32_t>(advancedConfig_.uinputShiftSelectDelayMs.value()),
      static_cast<uint32_t>(
          advancedConfig_.uinputShiftSelectAckTimeoutMs.value()),
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
      static_cast<uint32_t>(
          advancedConfig_.ibusAfterUinputShiftSelectWaitMs.value()),
      static_cast<uint32_t>(advancedConfig_.surroundingWaitMs.value()),
      static_cast<uint32_t>(advancedConfig_.surroundingProbeTimeoutMs.value()),
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
      static_cast<uint32_t>(
          advancedConfig_.ibusAfterXTestBackspaceWaitMs.value()),
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
  if (focusTracker_)
    focusTracker_->setDebugEnabled(debugEnabled());
  const auto previousOverrides = appBackendOverridesMap_;
  const PresentationMode previousDefaultMode = activePresentationMode_;
  const PresentationMode requestedMode = config_.presentationMode.value();
  rebuildAppBackendOverridesMap();
  // Xin quyền libei cho Native trước lượt rewrite đầu tiên khi có ứng dụng
  // chọn Native.
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
  const bool revertMacroOnBackspace = config_.revertMacroOnBackspace.value();
  const bool autoCapitalize = config_.autoCapitalizeAfterPunctuation.value();
  const auto macros = macroDefinitions();
  instance_->inputContextManager().foreach (
      [this, &inputMethod, spellCheck, realtimeSpellcheck, modernStyle,
       &outputCharset, macroEnabled, capitalizeMacro, revertMacroOnBackspace,
       autoCapitalize, &macros, &previousOverrides, previousDefaultMode,
       requestedMode](fcitx::InputContext *inputContext) {
        auto *rewriteState = rewriteHandler_.stateFor(*inputContext);
        const std::string program = resolveProgram(*inputContext, rewriteState);
        auto modeFor = [&program](const auto &overrides,
                                  PresentationMode fallback) {
          const auto it = overrides.find(program);
          return it == overrides.end()
                     ? fallback
                     : presentationModeForAppSetting(it->second, fallback);
        };
        const PresentationMode previousMode =
            modeFor(previousOverrides, previousDefaultMode);
        const PresentationMode nextMode =
            modeFor(appBackendOverridesMap_, requestedMode);
        if (previousMode != nextMode) {
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
              state->revertMacroOnBackspace == revertMacroOnBackspace &&
              state->macroRevision == macroRevision_) {
            return;
          }
          try {
            resetMode();
            state->engine = std::make_unique<BambooEngineAdapter>(
                inputMethod, spellCheck, realtimeSpellcheck, modernStyle,
                outputCharset, macroEnabled, capitalizeMacro, macros,
                revertMacroOnBackspace);
            state->inputMethod = inputMethod;
            state->spellCheck = spellCheck;
            state->realtimeSpellcheck = realtimeSpellcheck;
            state->modernStyle = modernStyle;
            state->outputCharset = outputCharset;
            state->macroEnabled = macroEnabled;
            state->capitalizeMacro = capitalizeMacro;
            state->revertMacroOnBackspace = revertMacroOnBackspace;
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

} // namespace areca
