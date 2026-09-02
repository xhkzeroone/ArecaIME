#include "rewrite_mode.h"

#include <exception>
#include <utility>

#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>

namespace areca {
namespace {

fcitx::KeySym normalizeRewriteKeypadSym(fcitx::KeySym sym) {
  if (sym >= FcitxKey_KP_0 && sym <= FcitxKey_KP_9) {
    return static_cast<fcitx::KeySym>(FcitxKey_0 + (sym - FcitxKey_KP_0));
  }
  switch (sym) {
  case FcitxKey_KP_Add:
    return FcitxKey_plus;
  case FcitxKey_KP_Subtract:
    return FcitxKey_minus;
  case FcitxKey_KP_Divide:
    return FcitxKey_slash;
  case FcitxKey_KP_Multiply:
    return FcitxKey_asterisk;
  case FcitxKey_KP_Decimal:
    return FcitxKey_period;
  case FcitxKey_KP_Enter:
    return FcitxKey_Return;
  case FcitxKey_KP_Equal:
    return FcitxKey_equal;
  case FcitxKey_KP_Space:
    return FcitxKey_space;
  default:
    return sym;
  }
}

bool hasRewriteShortcutModifier(const fcitx::Key &key) {
  const auto states = key.states();
  return states.test(fcitx::KeyState::Ctrl) ||
         states.test(fcitx::KeyState::Alt) ||
         states.test(fcitx::KeyState::Super) ||
         states.test(fcitx::KeyState::Meta) ||
         states.test(fcitx::KeyState::Hyper) ||
         states.test(fcitx::KeyState::Super2) ||
         states.test(fcitx::KeyState::Hyper2);
}

bool isSelectAllShortcut(const fcitx::Key &rawKey) {
  return rawKey.states().test(fcitx::KeyState::Ctrl) &&
         (rawKey.sym() == FcitxKey_a || rawKey.sym() == FcitxKey_A);
}

} // namespace

RewriteInputState::RewriteInputState(std::string inputMethod, bool spellCheck,
                                     bool realtimeSpellcheck,
                                     bool modernStyle,
                                     std::string outputCharset,
                                     bool macroEnabled, bool capitalizeMacro,
                                     uint64_t macroRevision,
                                     std::vector<MacroDefinition> macros)
    : inputMethod(std::move(inputMethod)), spellCheck(spellCheck),
      realtimeSpellcheck(realtimeSpellcheck),
      modernStyle(modernStyle), outputCharset(std::move(outputCharset)),
      macroEnabled(macroEnabled), capitalizeMacro(capitalizeMacro),
      macroRevision(macroRevision),
      engine(std::make_unique<BambooEngineAdapter>(
          this->inputMethod, this->spellCheck, this->realtimeSpellcheck,
          this->modernStyle, this->outputCharset, this->macroEnabled,
          this->capitalizeMacro, std::move(macros))) {}

RewriteModeHandler::RewriteModeHandler(fcitx::EventLoop &eventLoop,
                                       StateFactory &stateFactory,
                                       InputScheduler &scheduler,
                                       BoolProvider autoCapitalizeProvider,
                                       BoolProvider debugProvider,
                                       BackendVerdictProtector
                                           backendVerdictProtector,
                                       BackspaceRecoveryProvider
                                           backspaceRecoveryProvider)
    : eventLoop_(eventLoop), stateFactory_(stateFactory), scheduler_(scheduler),
      autoCapitalizeProvider_(std::move(autoCapitalizeProvider)),
      debugProvider_(std::move(debugProvider)),
      backendVerdictProtector_(std::move(backendVerdictProtector)),
      backspaceRecoveryProvider_(std::move(backspaceRecoveryProvider)) {}

RewriteModeHandler::~RewriteModeHandler() {}

RewriteInputState *
RewriteModeHandler::stateFor(fcitx::InputContext &inputContext) const {
  return inputContext.propertyFor(&stateFactory_);
}

void RewriteModeHandler::activate(fcitx::InputContext &inputContext) {
  stateFor(inputContext);
}

void RewriteModeHandler::deactivate(fcitx::InputContext &inputContext) {
  resetContext(inputContext);
}

bool RewriteModeHandler::syncEngineBackspace(RewriteInputState &state) {
  try {
    state.engine->backspace();
    return true;
  } catch (const std::exception &error) {
    FCITX_ERROR() << "areca: rewrite Backspace failed: " << error.what();
    state.engine->reset();
    return false;
  }
}

void RewriteModeHandler::forwardSyncedBackspace(fcitx::KeyEvent &event,
                                                RewriteInputState &state) {
  syncEngineBackspace(state);
  event.forward();
}

bool RewriteModeHandler::shouldRecoverBackspace(
    fcitx::InputContext &inputContext, RewriteInputState &state) const {
  const auto &currentText = state.engine->currentText();
  const auto queuedKeys = scheduler_.queuedKeyCount();
  if (currentText.empty() && queuedKeys == 0) {
    if (debugProvider_()) {
      const char *frontend = inputContext.frontend();
      FCITX_INFO() << "areca: backspace route=forward"
                   << " reason=no-composition"
                   << " queue=" << queuedKeys
                   << " program=" << inputContext.program()
                   << " frontend=" << (frontend ? frontend : "");
    }
    return false;
  }
  const bool capture = backspaceRecoveryProvider_();
  if (debugProvider_()) {
    const char *frontend = inputContext.frontend();
    FCITX_INFO() << "areca: backspace route="
                 << (capture ? "recovery" : "forward")
                 << " reason="
                 << (capture ? "composition-active" : "gate-disabled")
                 << " current=" << currentText
                 << " queue=" << queuedKeys
                 << " program=" << inputContext.program()
                 << " frontend=" << (frontend ? frontend : "");
  }
  return capture;
}

void RewriteModeHandler::deferBackspaceRecoveryUntilRelease(
    fcitx::KeyEvent &event, RewriteInputState &state) {
  state.backspaceRecoveryAwaitingRelease = true;
  if (debugProvider_()) {
    auto *inputContext = event.inputContext();
    const char *frontend =
        inputContext && inputContext->frontend() ? inputContext->frontend() : "";
    FCITX_INFO() << "areca: backspace recovery deferred until release"
                 << " current=" << state.engine->currentText()
                 << " queue=" << scheduler_.queuedKeyCount()
                 << " program="
                 << (inputContext ? inputContext->program() : "")
                 << " frontend=" << frontend;
  }
  event.filterAndAccept();
}

void RewriteModeHandler::runDeferredBackspaceRecovery(
    fcitx::InputContext &inputContext, fcitx::KeyEvent &event,
    RewriteInputState &state) {
  state.backspaceRecoveryAwaitingRelease = false;
  if (debugProvider_()) {
    const char *frontend = inputContext.frontend();
    FCITX_INFO() << "areca: backspace recovery resumed after release"
                 << " current=" << state.engine->currentText()
                 << " queue=" << scheduler_.queuedKeyCount()
                 << " program=" << inputContext.program()
                 << " frontend=" << (frontend ? frontend : "");
  }
  event.filterAndAccept();
  scheduler_.enqueueBackspace(inputContext);
}

void RewriteModeHandler::handleKeyEvent(fcitx::KeyEvent &event) {
  auto *inputContext = event.inputContext();
  if (!inputContext) {
    return;
  }
  auto *state = stateFor(*inputContext);
  const auto key = event.key();
  const auto normalizedKey = key.normalize();
  const auto rawKey = event.rawKey();
  const auto rawSym = rawKey.sym();
  const bool isBackspace =
      key.check(FcitxKey_BackSpace) || rawKey.check(FcitxKey_BackSpace);
  const auto textSym = normalizeRewriteKeypadSym(normalizedKey.sym());
  const bool isEnter = textSym == FcitxKey_Return ||
                       rawSym == FcitxKey_Return || rawSym == FcitxKey_KP_Enter;

  if (debugProvider_() && (!event.isRelease() || isBackspace || isEnter)) {
    FCITX_INFO() << "areca: rewrite handler key=" << key.toString()
                 << " release=" << event.isRelease()
                 << " pending=" << scheduler_.rewritePending()
                 << " queue=" << scheduler_.queuedKeyCount();
  }
  if (event.isRelease()) {
    if (isBackspace && state && state->engine &&
        state->backspaceRecoveryAwaitingRelease) {
      runDeferredBackspaceRecovery(*inputContext, event, *state);
    }
    return;
  }
  if (rawKey.isModifier()) {
    return;
  }
  if (isBackspace && scheduler_.rewritePending()) {
    if (debugProvider_()) {
      FCITX_INFO() << "areca: forward pending Backspace without mutating state";
    }
    event.forward();
    return;
  }
  if (inputContext->capabilityFlags().test(fcitx::CapabilityFlag::Password)) {
    resetContext(*inputContext);
    event.forward();
    return;
  }
  if (!state || !state->engine) {
    event.forward();
    return;
  }

  if (isBackspace) {
    backendVerdictProtector_(*inputContext, "user-backspace");
  } else if ((key.isCursorMove() || isSelectAllShortcut(rawKey)) &&
             !scheduler_.shouldRejectReset()) {
    backendVerdictProtector_(*inputContext,
                             "selection-or-navigation-shortcut");
  }

  const bool resetAndForward =
      key.isCursorMove() || rawSym == FcitxKey_Tab ||
      rawSym == FcitxKey_KP_Tab || rawSym == FcitxKey_ISO_Left_Tab ||
      rawSym == FcitxKey_Escape || hasRewriteShortcutModifier(rawKey);
  if (resetAndForward) {
    if (scheduler_.shouldRejectReset()) {
      // Cursor/modifier keys injected by uinput during or within post-commit window of a rewrite.
      // Forward without resetting state.
      event.forward();
      return;
    }
    state->sentenceCapitalization.reset();
    state->engine->reset();
    event.forward();
    return;
  }
  if (textSym == FcitxKey_Delete || rawSym == FcitxKey_Delete) {
    state->sentenceCapitalization.reset();
    event.forward();
    return;
  }
  if (isBackspace) {
    state->sentenceCapitalization.reset();
    if (state->backspaceRecoveryAwaitingRelease) {
      if (debugProvider_()) {
        const char *frontend = inputContext->frontend();
        FCITX_INFO() << "areca: backspace route=recovery"
                     << " reason=awaiting-release"
                     << " current=" << state->engine->currentText()
                     << " queue=" << scheduler_.queuedKeyCount()
                     << " program=" << inputContext->program()
                     << " frontend=" << (frontend ? frontend : "");
      }
      event.filterAndAccept();
      return;
    }
    if (shouldRecoverBackspace(*inputContext, *state)) {
      deferBackspaceRecoveryUntilRelease(event, *state);
      return;
    }
    forwardSyncedBackspace(event, *state);
    return;
  }
  if (isEnter) {
    state->sentenceCapitalization.reset();
    state->engine->reset();
    event.forward();
    return;
  }

  auto effectiveTextSym = textSym;
  if (autoCapitalizeProvider_()) {
    effectiveTextSym = capitalizeAfterSentenceBoundary(
        state->sentenceCapitalization, effectiveTextSym);
  } else {
    state->sentenceCapitalization.reset();
  }
  const uint32_t codepoint = fcitx::Key::keySymToUnicode(effectiveTextSym);
  const auto utf8Text = fcitx::Key::keySymToUTF8(effectiveTextSym);
  if (!codepoint || utf8Text.empty()) {
    event.forward();
    return;
  }
  event.filterAndAccept();
  scheduler_.enqueue(*inputContext, codepoint, utf8Text);
}

void RewriteModeHandler::requestProtectedReset(
    fcitx::InputContext &inputContext) {
  if (scheduler_.shouldRejectReset()) {
    if (debugProvider_()) {
      FCITX_INFO() << "areca: reset rejected (active rewrite or 50ms post-commit window)";
    }
    return;
  }
  resetContext(inputContext);
}

void RewriteModeHandler::resetContext(fcitx::InputContext &inputContext) {
  scheduler_.resetContext(inputContext);
  if (auto *state = stateFor(inputContext)) {
    state->backspaceRecoveryAwaitingRelease = false;
    state->sentenceCapitalization.reset();
  }
}

} // namespace areca
