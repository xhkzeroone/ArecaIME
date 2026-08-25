#pragma once

#include <string>

#include <fcitx/inputcontext.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>

#include "areca_config.h"
#include "autocomplete_surrounding_backend.h"
#include "bamboo_engine_adapter.h"
#include "forward_backspace_backend.h"
#include "input_scheduler.h"
#include "preedit_mode.h"
#include "redirect_mode.h"
#include "reliability_checker.h"
#include "rewrite_mode.h"
#include "surrounding_text_backend.h"
#include "uinput_backspace_backend.h"

namespace areca {

class ArecaEngine final : public fcitx::InputMethodEngineV2 {
public:
  explicit ArecaEngine(fcitx::Instance *instance);
  ~ArecaEngine() override;

  void keyEvent(const fcitx::InputMethodEntry &entry,
                fcitx::KeyEvent &event) override;
  std::string subMode(const fcitx::InputMethodEntry &entry,
                      fcitx::InputContext &inputContext) override;
  std::string subModeIconImpl(const fcitx::InputMethodEntry &entry,
                              fcitx::InputContext &inputContext) override;
  std::string subModeLabelImpl(const fcitx::InputMethodEntry &entry,
                               fcitx::InputContext &inputContext) override;
  void activate(const fcitx::InputMethodEntry &entry,
                fcitx::InputContextEvent &event) override;
  void deactivate(const fcitx::InputMethodEntry &entry,
                  fcitx::InputContextEvent &event) override;
  void reset(const fcitx::InputMethodEntry &entry,
             fcitx::InputContextEvent &event) override;

  const fcitx::Configuration *getConfig() const override;
  const fcitx::Configuration *
  getSubConfig(const std::string &path) const override;
  void setConfig(const fcitx::RawConfig &config) override;
  void setSubConfig(const std::string &path,
                    const fcitx::RawConfig &config) override;
  void reloadConfig() override;
  void save() override;

private:
  InputModeHandler &activeHandler();
  static const char *presentationModeName(PresentationMode mode);
  void switchPresentationMode(fcitx::InputContext &inputContext);
  SchedulerTiming timing() const;
  bool debugEnabled() const { return config_.debug.value(); }
  RewriteBackendSelection
  selectRewriteBackend(fcitx::InputContext &inputContext,
                       const BambooResult &result);
  bool shouldCaptureBackspaceRecovery(fcitx::InputContext &inputContext,
                                      const std::string &shownText);
  void ensureBackendVerdictContext(fcitx::InputContext &inputContext,
                                   bool logSwitch);
  void protectBackendVerdict(fcitx::InputContext &inputContext,
                             const char *reason);
  void clearBackendVerdictForLifecycle(fcitx::InputContext &inputContext,
                                       const char *eventName);
  bool backendVerdictProtected(fcitx::InputContext &inputContext) const;
  void scheduleUinputWarmup();
  void applyConfig();
  std::vector<MacroDefinition> macroDefinitions() const;

  fcitx::Instance *instance_;
  ArecaConfig config_;
  AdvancedConfig advancedConfig_;
  MacroTableConfig macroTable_;
  uint64_t macroRevision_ = 1;
  PresentationMode activePresentationMode_ = PresentationMode::Rewrite;
  fcitx::FactoryFor<RewriteInputState> rewriteStateFactory_;
  fcitx::FactoryFor<PreeditInputState> preeditStateFactory_;
  ReliabilityChecker reliabilityChecker_;
  bool backendVerdictContextKnown_ = false;
  fcitx::ICUUID backendVerdictContextId_{};
  SurroundingReliabilityState backendVerdict_;
  uint64_t backendVerdictProtectedUntil_ = 0;
  SurroundingTextBackend surroundingBackend_;
  AutocompleteForwardSurroundingBackend autocompleteForwardBackend_;
  AutocompleteForwardSurroundingBackend autocompleteEdgeForwardBackend_;
  ForwardBackspaceBackend forwardBackspaceBackend_;
  UinputBackspaceBackend uinputBackspaceBackend_;
  std::unique_ptr<fcitx::EventSourceTime> uinputWarmupTimer_;
  InputScheduler scheduler_;
  RewriteModeHandler rewriteHandler_;
  PreeditModeHandler preeditHandler_;
  RedirectModeHandler redirectHandler_;
};

} // namespace areca
