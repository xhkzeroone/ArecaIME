#include "preedit_mode.h"

#include <exception>
#include <utility>

#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/inputpanel.h>
#include <fcitx/surroundingtext.h>

#include "preedit_logic.h"
#include "surrounding_text_cache.h"
#include "surrounding_text_restore.h"

namespace areca {
namespace {

fcitx::KeySym normalizePreeditKeypadSym(fcitx::KeySym sym) {
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

bool hasPreeditShortcutModifier(const fcitx::Key &key) {
  const auto states = key.states();
  return states.test(fcitx::KeyState::Ctrl) ||
         states.test(fcitx::KeyState::Alt) ||
         states.test(fcitx::KeyState::Super) ||
         states.test(fcitx::KeyState::Meta) ||
         states.test(fcitx::KeyState::Hyper) ||
         states.test(fcitx::KeyState::Super2) ||
         states.test(fcitx::KeyState::Hyper2);
}

} // namespace

PreeditInputState::PreeditInputState(std::string inputMethod, bool spellCheck,
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

PreeditModeHandler::PreeditModeHandler(
    fcitx::EventLoop &eventLoop, StateFactory &stateFactory,
    DebugProvider debugProvider, AutoCapitalizeProvider autoCapitalizeProvider,
    RestoreSurroundingTextProvider restoreSurroundingTextProvider)
    : eventLoop_(eventLoop), stateFactory_(stateFactory),
      debugProvider_(std::move(debugProvider)),
      autoCapitalizeProvider_(std::move(autoCapitalizeProvider)),
      restoreSurroundingTextProvider_(
          std::move(restoreSurroundingTextProvider)) {}

PreeditModeHandler::~PreeditModeHandler() { lifetime_.reset(); }

PreeditInputState *
PreeditModeHandler::stateFor(fcitx::InputContext &inputContext) const {
  return inputContext.propertyFor(&stateFactory_);
}

void PreeditModeHandler::activate(fcitx::InputContext &inputContext) {
  resetContext(inputContext);
}

void PreeditModeHandler::deactivate(fcitx::InputContext &inputContext) {
  if (auto *state = stateFor(inputContext)) {
    cancelProtectedReset(inputContext);
    commitComposition(inputContext, *state);
    state->sentenceCapitalization.reset();
  }
}

void PreeditModeHandler::handleKeyEvent(fcitx::KeyEvent &event) {
  auto *inputContext = event.inputContext();
  if (!inputContext) {
    return;
  }
  const auto rawKey = event.rawKey();
  if (event.isRelease() || rawKey.isModifier()) {
    return;
  }

  auto *state = stateFor(*inputContext);
  if (!state || !state->engine) {
    event.forward();
    return;
  }
  cancelProtectedReset(*inputContext);

  if (inputContext->capabilityFlags().test(fcitx::CapabilityFlag::Password)) {
    resetContext(*inputContext);
    event.forward();
    return;
  }

  const auto key = event.key();
  const auto normalizedKey = key.normalize();
  const auto rawSym = rawKey.sym();
  const auto textSym = normalizePreeditKeypadSym(normalizedKey.sym());
  const bool isBackspace =
      key.check(FcitxKey_BackSpace) || rawKey.check(FcitxKey_BackSpace);
  const bool isEnter = textSym == FcitxKey_Return ||
                       rawSym == FcitxKey_Return || rawSym == FcitxKey_KP_Enter;

  // Modified and unsupported keys are not rebuilt. Commit the current Bamboo
  // preedit, then let Fcitx forward the original event with its exact modifier
  // and frontend metadata.
  if (hasPreeditShortcutModifier(normalizedKey)) {
    commitComposition(*inputContext, *state);
    state->sentenceCapitalization.reset();
    event.forward();
    return;
  }

  if (isBackspace) {
    state->sentenceCapitalization.reset();
    if (state->composing.empty()) {
      state->engine->reset();
      event.forward();
      return;
    }
    try {
      state->engine->backspace();
      state->composing = state->engine->currentText();
      updatePreedit(*inputContext, *state);
      event.filterAndAccept();
    } catch (const std::exception &error) {
      FCITX_ERROR() << "areca: preedit Backspace failed: " << error.what();
      clearComposition(*inputContext, *state);
      event.forward();
    }
    return;
  }

  const bool commitAndForward =
      key.isCursorMove() || textSym == FcitxKey_Delete ||
      rawSym == FcitxKey_Delete || rawSym == FcitxKey_Tab ||
      rawSym == FcitxKey_KP_Tab || rawSym == FcitxKey_ISO_Left_Tab ||
      rawSym == FcitxKey_Escape || isEnter;
  if (commitAndForward) {
    commitComposition(*inputContext, *state);
    state->sentenceCapitalization.reset();
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
    commitComposition(*inputContext, *state);
    state->sentenceCapitalization.reset();
    event.forward();
    return;
  }

  const bool restoredBeforeKey =
      tryRestoreFromSurroundingText(*inputContext, *state);
  try {
    const auto result = state->engine->process(codepoint, utf8Text);
    if (!result.newText.empty()) {
      state->composing = result.newText;
      updatePreedit(*inputContext, *state);
      event.filterAndAccept();
      if (debugProvider_()) {
        FCITX_INFO() << "areca: preedit update text=" << state->composing;
      }
      return;
    }

    const auto committed = buildPreeditCommit(result);
    const bool nativeBoundary =
        result.currentText.empty() && committed == utf8Text;
    if (nativeBoundary) {
      state->engine->reset();
      state->composing.clear();
      updatePreedit(*inputContext, *state);
      event.forward();
      return;
    }

    if (!committed.empty()) {
      inputContext->commitString(committed);
    }
    state->engine->reset();
    state->composing.clear();
    updatePreedit(*inputContext, *state);
    event.filterAndAccept();
  } catch (const std::exception &error) {
    FCITX_ERROR() << "areca: preedit Bamboo processing failed: "
                  << error.what();
    // Nếu từ cũ đã bị kéo khỏi ứng dụng vào preedit, commit nó trả lại trước
    // khi forward phím lỗi để không làm mất nội dung của người dùng.
    if (restoredBeforeKey) {
      commitComposition(*inputContext, *state);
    } else {
      clearComposition(*inputContext, *state);
    }
    event.forward();
  }
}

bool PreeditModeHandler::tryRestoreFromSurroundingText(
    fcitx::InputContext &inputContext, PreeditInputState &state) {
  if (!state.surroundingRestoreArmed) {
    return false;
  }
  state.surroundingRestoreArmed = false;

  const bool supported =
      restoreSurroundingTextProvider_() && state.outputCharset == "Unicode" &&
      state.engine && state.composing.empty() &&
      state.engine->currentText().empty() &&
      !inputContext.capabilityFlags().test(fcitx::CapabilityFlag::Password) &&
      inputContext.capabilityFlags().test(
          fcitx::CapabilityFlag::SurroundingText);
  if (!supported) {
    return false;
  }

  const auto &surrounding = inputContext.surroundingText();
  if (!surrounding.isValid()) {
    return false;
  }
  const auto candidate = extractSurroundingRestoreCandidate(
      surrounding.text(), surrounding.cursor(), surrounding.anchor());
  if (!candidate ||
      !state.engine->restoreFromRenderedText(candidate->text)) {
    return false;
  }

  // Preedit sẽ tự hiển thị và commit lại cả từ. Xóa bản đã commit trước khi đưa
  // nó vào composition để frontend không hiển thị hai bản giống nhau.
  inputContext.deleteSurroundingText(
      -static_cast<int>(candidate->characterCount), candidate->characterCount);
  updateSurroundingCacheAfterDelete(
      inputContext, -static_cast<int>(candidate->characterCount),
      candidate->characterCount);
  state.composing = candidate->text;
  if (debugProvider_()) {
    FCITX_INFO() << "areca: preedit surrounding restore applied"
                 << " text=" << candidate->text
                 << " chars=" << candidate->characterCount;
  }
  return true;
}

void PreeditModeHandler::updatePreedit(fcitx::InputContext &inputContext,
                                       const PreeditInputState &state) {
  auto &panel = inputContext.inputPanel();
  panel.reset();
  if (!state.composing.empty() && fcitx::utf8::validate(state.composing)) {
    fcitx::Text text;
    text.append(state.composing);
    text.setCursor(static_cast<int>(text.textLength()));
    if (inputContext.capabilityFlags().test(fcitx::CapabilityFlag::Preedit)) {
      panel.setClientPreedit(text);
    } else {
      panel.setPreedit(text);
    }
  }
  inputContext.updatePreedit();
  inputContext.updateUserInterface(fcitx::UserInterfaceComponent::InputPanel,
                                   true);
}

void PreeditModeHandler::clearComposition(fcitx::InputContext &inputContext,
                                          PreeditInputState &state) {
  state.engine->reset();
  state.composing.clear();
  state.sentenceCapitalization.reset();
  updatePreedit(inputContext, state);
}

void PreeditModeHandler::commitComposition(fcitx::InputContext &inputContext,
                                           PreeditInputState &state) {
  if (!state.composing.empty()) {
    inputContext.commitString(state.composing);
  }
  state.engine->reset();
  state.composing.clear();
  updatePreedit(inputContext, state);
}

void PreeditModeHandler::requestProtectedReset(
    fcitx::InputContext &inputContext) {
  resetContext(inputContext);
}

void PreeditModeHandler::cancelProtectedReset(
    fcitx::InputContext &inputContext) {
  if (auto *state = stateFor(inputContext)) {
    state->delayedResetTimer.reset();
  }
}

void PreeditModeHandler::resetContext(fcitx::InputContext &inputContext) {
  if (auto *state = stateFor(inputContext)) {
    state->delayedResetTimer.reset();
    clearComposition(inputContext, *state);
    // Reset/focus/click có thể đặt con trỏ vào một từ đã commit. Cho phép phím
    // text kế tiếp thử chuyển từ đó vào preedit nếu người dùng bật tính năng.
    state->surroundingRestoreArmed = true;
  }
}

} // namespace areca
