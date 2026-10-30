#include "preedit_mode.h"

#include <exception>
#include <string_view>
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

constexpr uint64_t kPostCommitResetProtectionUsec = 20 * 1000;

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
                                     std::vector<MacroDefinition> macros,
                                     bool revertMacroOnBackspace)
    : inputMethod(std::move(inputMethod)), spellCheck(spellCheck),
      realtimeSpellcheck(realtimeSpellcheck),
      modernStyle(modernStyle), outputCharset(std::move(outputCharset)),
      macroEnabled(macroEnabled), capitalizeMacro(capitalizeMacro),
      revertMacroOnBackspace(revertMacroOnBackspace),
      macroRevision(macroRevision),
      engine(std::make_unique<BambooEngineAdapter>(
          this->inputMethod, this->spellCheck, this->realtimeSpellcheck,
          this->modernStyle, this->outputCharset, this->macroEnabled,
          this->capitalizeMacro, std::move(macros),
          this->revertMacroOnBackspace)) {}

PreeditModeHandler::PreeditModeHandler(
    StateFactory &stateFactory, DebugProvider debugProvider,
    AutoCapitalizeProvider autoCapitalizeProvider,
    BackspaceRecoveryProvider backspaceRecoveryProvider,
    RestoreSurroundingTextProvider restoreSurroundingTextProvider,
    MacroBypassMatcher macroBypassMatcher,
    FinalizedWordEraser finalizedWordEraser)
    : stateFactory_(stateFactory), debugProvider_(std::move(debugProvider)),
      autoCapitalizeProvider_(std::move(autoCapitalizeProvider)),
      backspaceRecoveryProvider_(std::move(backspaceRecoveryProvider)),
      restoreSurroundingTextProvider_(
          std::move(restoreSurroundingTextProvider)),
      macroBypassMatcher_(std::move(macroBypassMatcher)),
      finalizedWordEraser_(std::move(finalizedWordEraser)) {}

PreeditInputState *
PreeditModeHandler::stateFor(fcitx::InputContext &inputContext) const {
  return inputContext.propertyFor(&stateFactory_);
}

void PreeditModeHandler::activate(fcitx::InputContext &inputContext) {
  // Bắt đầu: Giữ finalized-word recovery qua lifecycle giả của Edge X11
  if (auto *state = stateFor(inputContext);
      state && state->finalizedWordRecovery ==
                   FinalizedWordRecoveryState::Erasing) {
    if (debugProvider_()) {
      FCITX_INFO() << "areca: preedit activate reset rejected"
                   << " reason=finalized-word-recovery";
    }
    return;
  }
  // Kết thúc: Giữ finalized-word recovery qua lifecycle giả của Edge X11
  resetContext(inputContext);
}

void PreeditModeHandler::deactivate(fcitx::InputContext &inputContext) {
  if (auto *state = stateFor(inputContext)) {
    // Bắt đầu: Giữ finalized-word recovery qua lifecycle giả của Edge X11
    if (state->finalizedWordRecovery == FinalizedWordRecoveryState::Erasing) {
      if (debugProvider_()) {
        FCITX_INFO() << "areca: preedit deactivate rejected"
                     << " reason=finalized-word-recovery";
      }
      return;
    }
    // Kết thúc: Giữ finalized-word recovery qua lifecycle giả của Edge X11
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
  auto *state = stateFor(*inputContext);
  if (!state || !state->engine) {
    event.forward();
    return;
  }
  const auto key = event.key();
  const bool isBackspace =
      key.check(FcitxKey_BackSpace) || rawKey.check(FcitxKey_BackSpace);
  const bool isRepeat = key.states().test(fcitx::KeyState::Repeat) ||
                        rawKey.states().test(fcitx::KeyState::Repeat);
  if (event.isRelease()) {
    if (isBackspace && state->finalizedWordRecovery ==
                           FinalizedWordRecoveryState::AwaitingBackspaceRelease) {
      if (isRepeat) {
        event.filterAndAccept();
      } else {
        state->finalizedWordRecovery = FinalizedWordRecoveryState::Idle;
        event.filterAndAccept();
        if (!startFinalizedWordRecovery(*inputContext, *state)) {
          state->engine->reset();
        }
      }
    }
    return;
  }
  if (rawKey.isModifier()) {
    return;
  }
  // Backspace do backend bơm vào phải tiếp tục đi tới ứng dụng, nhưng không
  // được làm thay đổi state Bamboo trong lúc từ cũ đang được xóa.
  if (state->finalizedWordRecovery == FinalizedWordRecoveryState::Erasing) {
    event.forward();
    return;
  }

  if (inputContext->capabilityFlags().test(fcitx::CapabilityFlag::Password)) {
    resetContext(*inputContext);
    event.forward();
    return;
  }

  const auto normalizedKey = key.normalize();
  const auto rawSym = rawKey.sym();
  const auto textSym = normalizePreeditKeypadSym(normalizedKey.sym());
  const bool suppressMacro = !state->engine->currentText().empty() &&
                             macroBypassMatcher_(normalizedKey);
  const bool isEnter = textSym == FcitxKey_Return ||
                       rawSym == FcitxKey_Return || rawSym == FcitxKey_KP_Enter;

  // Modified and unsupported keys are not rebuilt. Commit the current Bamboo
  // preedit, then let Fcitx forward the original event with its exact modifier
  // and frontend metadata.
  if (!suppressMacro && hasPreeditShortcutModifier(normalizedKey)) {
    commitComposition(*inputContext, *state);
    state->sentenceCapitalization.reset();
    event.forward();
    return;
  }

  if (isBackspace) {
    // IBus có thể gửi press lặp không có Repeat khi đang chờ khôi phục từ.
    const char *frontend = inputContext->frontend();
    const bool ibusHeldPress =
        state->finalizedWordRecovery ==
            FinalizedWordRecoveryState::AwaitingBackspaceRelease &&
        frontend && std::string_view(frontend) == "ibus";
    state->sentenceCapitalization.reset();
    if (isRepeat || ibusHeldPress) {
      if (debugProvider_()) {
        FCITX_INFO() << "areca: preedit backspace route=forward"
                     << " reason=repeat-reset-composition"
                     << " repeat=" << isRepeat
                     << " inferred_ibus_repeat=" << ibusHeldPress;
      }
      // Giữ nguyên phần chữ người dùng đang thấy, nhưng bỏ toàn bộ trạng thái
      // Bamboo để chuỗi Backspace repeat tiếp tục xóa trực tiếp trong app.
      if (state->composing.empty()) {
        clearComposition(*inputContext, *state);
      } else {
        commitComposition(*inputContext, *state);
      }
      state->finalizedWordRecovery = FinalizedWordRecoveryState::Idle;
      state->surroundingRestoreArmed = false;
      event.forward();
      return;
    }
    if (state->engine && state->engine->canUndoMacro()) {
      try {
        const auto result = state->engine->processBackspace();
        auto &surrounding = inputContext->surroundingText();
        if (surrounding.isValid() && result.deleteCount > 0) {
          inputContext->deleteSurroundingText(
              -static_cast<int>(result.deleteCount), result.deleteCount);
          updateSurroundingCacheAfterDelete(
              *inputContext, -static_cast<int>(result.deleteCount),
              result.deleteCount);
          if (!result.commitText.empty()) {
            inputContext->commitString(result.commitText);
            updateSurroundingCacheAfterCommit(*inputContext, result.commitText);
          }
          event.filterAndAccept();
          return;
        }
      } catch (const std::exception &error) {
        FCITX_ERROR() << "areca: preedit macro undo failed: " << error.what();
      }
    }
    if (state->composing.empty()) {
      if (state->finalizedWordRecovery ==
          FinalizedWordRecoveryState::AwaitingBackspaceRelease) {
        if (isRepeat) {
          // Nhấn giữ Backspace có nghĩa là người dùng muốn xóa liên tục, không
          // mở lại từ trong preedit. Hủy snapshot và để repeat đi xuống app.
          state->finalizedWordRecovery = FinalizedWordRecoveryState::Idle;
          state->engine->reset();
          event.forward();
        } else {
          event.filterAndAccept();
        }
        return;
      }
      try {
        // Adapter đếm các boundary đã commit. Backspace qua boundary trung gian
        // chỉ được forward; khi chạm tới snapshot từ, handler kéo từ lên preedit.
        state->engine->backspace();
        if (state->engine->currentText().empty()) {
          event.forward();
        } else if (fcitx::utf8::validate(state->engine->currentText())) {
          state->finalizedWordRecovery =
              FinalizedWordRecoveryState::AwaitingBackspaceRelease;
          event.filterAndAccept();
        } else {
          state->engine->reset();
          event.forward();
        }
      } catch (const std::exception &error) {
        FCITX_ERROR() << "areca: preedit finalized-word Backspace failed: "
                      << error.what();
        clearComposition(*inputContext, *state);
        event.forward();
      }
      return;
    }
    try {
      if (backspaceRecoveryProvider_()) {
        // Recovery tái chạy chuỗi phím Latin còn lại qua Bamboo sau khi xóa;
        // đây là nhánh biến `nhanhsh` + Backspace thành `nhánh`.
        state->composing = state->engine->processBackspace().newText;
      } else {
        state->engine->backspace();
        state->composing = state->engine->currentText();
      }
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

  // Dùng rule của input method thay vì tự phân loại chữ/dấu. Boundary kết thúc
  // macro phải đi thẳng vào Bamboo; VNI/VIQR vẫn được restore bằng phím riêng.
  bool restoredBeforeKey = false;
  if (state->engine->canProcessKey(codepoint)) {
    restoredBeforeKey = tryRestoreFromSurroundingText(*inputContext, *state);
  } else {
    state->surroundingRestoreArmed = false;
  }
  try {
    if (suppressMacro && debugProvider_()) {
      FCITX_INFO() << "areca: preedit macro expansion bypassed";
    }
    const auto result = suppressMacro
                            ? state->engine->processWithoutMacro(codepoint,
                                                                utf8Text)
                            : state->engine->process(codepoint, utf8Text);
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
      state->composing.clear();
      updatePreedit(*inputContext, *state);
      event.forward();
      return;
    }

    if (!committed.empty()) {
      state->resetProtectedUntilUsec =
          fcitx::now(CLOCK_MONOTONIC) + kPostCommitResetProtectionUsec;
      inputContext->commitString(committed);
      updateSurroundingCacheAfterCommit(*inputContext, committed);
    }
    // Không reset ở boundary: adapter giữ snapshot của từ và số boundary đã
    // commit để Backspace có thể đi ngược nhiều Space giống mode Rewrite.
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

bool PreeditModeHandler::startFinalizedWordRecovery(
    fcitx::InputContext &inputContext, PreeditInputState &state) {
  const auto &word = state.engine->currentText();
  if (word.empty() || !fcitx::utf8::validate(word)) {
    return false;
  }
  const auto wordLength = static_cast<uint32_t>(fcitx::utf8::length(word));
  if (!wordLength) {
    return false;
  }

  const uint32_t deleteCount = wordLength + 1;
  if (!finalizedWordEraser_) {
    return false;
  }
  state.finalizedWordRecovery = FinalizedWordRecoveryState::Erasing;
  const auto watchedContext = inputContext.watch();
  const std::string recoveredWord = word;
  const bool started = finalizedWordEraser_(
      inputContext, deleteCount,
      [this, watchedContext, recoveredWord](bool succeeded) {
        auto *context = watchedContext.get();
        if (!context) {
          if (debugProvider_()) {
            FCITX_INFO() << "areca: preedit finalized-word callback ignored"
                         << " reason=context-lost";
          }
          return;
        }
        auto *currentState = stateFor(*context);
        if (!currentState || currentState->finalizedWordRecovery !=
                                 FinalizedWordRecoveryState::Erasing) {
          if (debugProvider_()) {
            FCITX_INFO() << "areca: preedit finalized-word callback ignored"
                         << " reason=state-changed"
                         << " has_state=" << (currentState != nullptr)
                         << " recovery_state="
                         << (currentState
                                 ? static_cast<int>(
                                       currentState->finalizedWordRecovery)
                                 : -1);
          }
          return;
        }
        currentState->finalizedWordRecovery = FinalizedWordRecoveryState::Idle;
        if (!succeeded) {
          currentState->engine->reset();
          currentState->composing.clear();
          updatePreedit(*context, *currentState);
          return;
        }
        currentState->composing = recoveredWord;
        updatePreedit(*context, *currentState);
        if (debugProvider_()) {
          FCITX_INFO() << "areca: preedit restored finalized word"
                       << " text=" << recoveredWord << " erase=backspace";
        }
      });
  if (!started) {
    state.finalizedWordRecovery = FinalizedWordRecoveryState::Idle;
  }
  return started;
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
  auto candidate = extractSurroundingRestoreCandidate(
      surrounding.text(), surrounding.cursor(), surrounding.anchor());
  while (candidate &&
         !state.engine->restoreFromRenderedText(candidate->text)) {
    candidate = shortenSurroundingRestoreCandidate(*candidate);
  }
  if (!candidate) {
    return false;
  }

  // Preedit sẽ tự hiển thị và commit lại đoạn Bamboo dựng được sát caret. Xóa
  // đúng đoạn đó khỏi ứng dụng; prefix/suffix còn lại của từ được giữ nguyên.
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
    updateSurroundingCacheAfterCommit(inputContext, state.composing);
  }
  state.engine->reset();
  state.composing.clear();
  updatePreedit(inputContext, state);
}

void PreeditModeHandler::requestProtectedReset(
    fcitx::InputContext &inputContext) {
  auto *state = stateFor(inputContext);
  // Bắt đầu: Giữ finalized-word recovery qua lifecycle giả của Edge X11
  if (state && state->finalizedWordRecovery ==
                   FinalizedWordRecoveryState::Erasing) {
    if (debugProvider_()) {
      FCITX_INFO() << "areca: preedit reset rejected"
                   << " reason=finalized-word-recovery";
    }
    return;
  }
  // Kết thúc: Giữ finalized-word recovery qua lifecycle giả của Edge X11
  if (state && fcitx::now(CLOCK_MONOTONIC) <=
                   state->resetProtectedUntilUsec) {
    if (debugProvider_()) {
      FCITX_INFO() << "areca: preedit reset rejected"
                   << " reason=post-commit-window";
    }
    return;
  }
  resetContext(inputContext);
}

void PreeditModeHandler::resetContext(fcitx::InputContext &inputContext) {
  if (auto *state = stateFor(inputContext)) {
    state->finalizedWordRecovery = FinalizedWordRecoveryState::Idle;
    state->resetProtectedUntilUsec = 0;
    clearComposition(inputContext, *state);
    // Reset/focus/click có thể đặt con trỏ vào một từ đã commit. Cho phép phím
    // text kế tiếp thử chuyển từ đó vào preedit nếu người dùng bật tính năng.
    state->surroundingRestoreArmed = true;
  }
}

bool PreeditModeHandler::prepareSurroundingRestoreAfterClick(
    fcitx::InputContext &inputContext) {
  auto *state = stateFor(inputContext);
  if (!state || !state->engine || !state->composing.empty() ||
      state->finalizedWordRecovery != FinalizedWordRecoveryState::Idle) {
    return false;
  }

  // Preedit trống vẫn có thể giữ snapshot của từ vừa commit trong Bamboo.
  // Bỏ snapshot đó để phím kế tiếp phục hồi phần từ nằm trước caret mới.
  state->engine->reset();
  state->sentenceCapitalization.reset();
  state->resetProtectedUntilUsec = 0;
  state->surroundingRestoreArmed = true;
  return true;
}

} // namespace areca
