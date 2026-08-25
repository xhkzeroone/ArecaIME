#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <fcitx-utils/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>

#include "bamboo_engine_adapter.h"
#include "input_scheduler.h"
#include "mode_handler.h"
#include "reliability_checker.h"
#include "sentence_capitalizer.h"

namespace areca {

struct RewriteInputState final : public fcitx::InputContextProperty {
  explicit RewriteInputState(std::string inputMethod, bool spellCheck,
                             bool realtimeSpellcheck,
                             bool modernStyle, std::string outputCharset,
                             bool macroEnabled, bool capitalizeMacro,
                             uint64_t macroRevision,
                             std::vector<MacroDefinition> macros);

  std::string inputMethod;
  bool spellCheck;
  bool realtimeSpellcheck;
  bool modernStyle;
  std::string outputCharset;
  bool macroEnabled;
  bool capitalizeMacro;
  uint64_t macroRevision;
  std::unique_ptr<VietnameseEngine> engine;
  SentenceCapitalizationState sentenceCapitalization;
  SurroundingReliabilityState surroundingReliability;
  bool backspaceRecoveryAwaitingRelease = false;
};

class RewriteModeHandler final : public InputModeHandler {
public:
  using StateFactory = fcitx::FactoryFor<RewriteInputState>;
  using BoolProvider = std::function<bool()>;
  using BackendVerdictProtector =
      std::function<void(fcitx::InputContext &, const char *)>;
  using BackspaceRecoveryGate =
      std::function<bool(fcitx::InputContext &, const std::string &)>;

  RewriteModeHandler(fcitx::EventLoop &eventLoop, StateFactory &stateFactory,
                     InputScheduler &scheduler,
                     BoolProvider autoCapitalizeProvider,
                     BoolProvider debugProvider,
                     BackendVerdictProtector backendVerdictProtector,
                     BackspaceRecoveryGate backspaceRecoveryGate);
  ~RewriteModeHandler();

  RewriteInputState *stateFor(fcitx::InputContext &inputContext) const;
  void activate(fcitx::InputContext &inputContext) override;
  void deactivate(fcitx::InputContext &inputContext) override;
  void handleKeyEvent(fcitx::KeyEvent &event) override;
  void requestProtectedReset(fcitx::InputContext &inputContext) override;
  void resetContext(fcitx::InputContext &inputContext) override;

private:
  bool syncEngineBackspace(RewriteInputState &state);
  void forwardSyncedBackspace(fcitx::KeyEvent &event,
                              RewriteInputState &state);
  bool shouldRecoverBackspace(fcitx::InputContext &inputContext,
                              RewriteInputState &state) const;
  void deferBackspaceRecoveryUntilRelease(fcitx::KeyEvent &event,
                                          RewriteInputState &state);
  void runDeferredBackspaceRecovery(fcitx::InputContext &inputContext,
                                    fcitx::KeyEvent &event,
                                    RewriteInputState &state);

  fcitx::EventLoop &eventLoop_;
  StateFactory &stateFactory_;
  InputScheduler &scheduler_;
  BoolProvider autoCapitalizeProvider_;
  BoolProvider debugProvider_;
  BackendVerdictProtector backendVerdictProtector_;
  BackspaceRecoveryGate backspaceRecoveryGate_;
};

} // namespace areca
