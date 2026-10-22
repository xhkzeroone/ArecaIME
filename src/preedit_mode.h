#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <fcitx-utils/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>

#include "bamboo_engine_adapter.h"
#include "mode_handler.h"
#include "sentence_capitalizer.h"

namespace areca {

enum class FinalizedWordRecoveryState {
  Idle,
  AwaitingBackspaceRelease,
  Erasing,
};

struct PreeditInputState final : public fcitx::InputContextProperty {
  explicit PreeditInputState(std::string inputMethod, bool spellCheck,
                             bool realtimeSpellcheck,
                             bool modernStyle, std::string outputCharset,
                             bool macroEnabled, bool capitalizeMacro,
                             uint64_t macroRevision,
                             std::vector<MacroDefinition> macros,
                             bool revertMacroOnBackspace = true);

  std::string inputMethod;
  bool spellCheck;
  bool realtimeSpellcheck;
  bool modernStyle;
  std::string outputCharset;
  bool macroEnabled;
  bool capitalizeMacro;
  bool revertMacroOnBackspace;
  uint64_t macroRevision;
  std::unique_ptr<VietnameseEngine> engine;
  std::string composing;
  SentenceCapitalizationState sentenceCapitalization;
  // Frontend vừa đổi text hoặc vị trí con trỏ. Phím text kế tiếp được phép thử
  // chuyển từ đã commit trước con trỏ trở lại thành preedit đúng một lần.
  bool surroundingRestoreArmed = false;
  FinalizedWordRecoveryState finalizedWordRecovery =
      FinalizedWordRecoveryState::Idle;
  uint64_t resetProtectedUntilUsec = 0;
};

class PreeditModeHandler final : public InputModeHandler {
public:
  using StateFactory = fcitx::FactoryFor<PreeditInputState>;
  using DebugProvider = std::function<bool()>;
  using AutoCapitalizeProvider = std::function<bool()>;
  using BackspaceRecoveryProvider = std::function<bool()>;
  using RestoreSurroundingTextProvider = std::function<bool()>;
  using FinalizedWordEraseDone = std::function<void(bool)>;
  using FinalizedWordEraser = std::function<bool(
      fcitx::InputContext &, uint32_t, FinalizedWordEraseDone)>;

  PreeditModeHandler(StateFactory &stateFactory, DebugProvider debugProvider,
                     AutoCapitalizeProvider autoCapitalizeProvider,
                     BackspaceRecoveryProvider backspaceRecoveryProvider,
                     RestoreSurroundingTextProvider
                         restoreSurroundingTextProvider,
                     FinalizedWordEraser finalizedWordEraser);
  void activate(fcitx::InputContext &inputContext) override;
  void deactivate(fcitx::InputContext &inputContext) override;
  void handleKeyEvent(fcitx::KeyEvent &event) override;
  void requestProtectedReset(fcitx::InputContext &inputContext) override;
  void resetContext(fcitx::InputContext &inputContext) override;

  PreeditInputState *stateFor(fcitx::InputContext &inputContext) const;

private:
  void updatePreedit(fcitx::InputContext &inputContext,
                     const PreeditInputState &state);
  void clearComposition(fcitx::InputContext &inputContext,
                        PreeditInputState &state);
  void commitComposition(fcitx::InputContext &inputContext,
                         PreeditInputState &state);
  bool tryRestoreFromSurroundingText(fcitx::InputContext &inputContext,
                                     PreeditInputState &state);
  bool startFinalizedWordRecovery(fcitx::InputContext &inputContext,
                                  PreeditInputState &state);

  StateFactory &stateFactory_;
  DebugProvider debugProvider_;
  AutoCapitalizeProvider autoCapitalizeProvider_;
  BackspaceRecoveryProvider backspaceRecoveryProvider_;
  RestoreSurroundingTextProvider restoreSurroundingTextProvider_;
  FinalizedWordEraser finalizedWordEraser_;
};

} // namespace areca
