#include "rewrite_mode.h"

#include <exception>
#include <string_view>
#include <utility>

#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <fcitx/surroundingtext.h>

#include "surrounding_probe_match.h"

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

// Phím không bị filter được ứng dụng tự xử lý (chèn, xoá, di chuyển) nên Areca
// không còn biết chắc text trước con trỏ.
class KnownTextGuard {
public:
  KnownTextGuard(fcitx::KeyEvent &event, RewriteInputState &state)
      : event_(event), state_(state) {}
  ~KnownTextGuard() {
    if (!event_.filtered() && !keep_) {
      state_.knownTextBeforeCursor.clear();
    }
  }
  KnownTextGuard(const KnownTextGuard &) = delete;
  KnownTextGuard &operator=(const KnownTextGuard &) = delete;

  // Phím đầu từ được forward nguyên vẹn: scheduler đã ghi nhận ký tự đó.
  void keep() { keep_ = true; }

private:
  fcitx::KeyEvent &event_;
  RewriteInputState &state_;
  bool keep_ = false;
};

} // namespace

RewriteInputState::RewriteInputState(std::string inputMethod, bool spellCheck,
                                     bool realtimeSpellcheck, bool modernStyle,
                                     std::string outputCharset,
                                     bool macroEnabled, bool capitalizeMacro,
                                     uint64_t macroRevision,
                                     std::vector<MacroDefinition> macros,
                                     bool revertMacroOnBackspace)
    : inputMethod(std::move(inputMethod)), spellCheck(spellCheck),
      realtimeSpellcheck(realtimeSpellcheck), modernStyle(modernStyle),
      outputCharset(std::move(outputCharset)), macroEnabled(macroEnabled),
      capitalizeMacro(capitalizeMacro),
      revertMacroOnBackspace(revertMacroOnBackspace),
      macroRevision(macroRevision),
      engine(std::make_unique<BambooEngineAdapter>(
          this->inputMethod, this->spellCheck, this->realtimeSpellcheck,
          this->modernStyle, this->outputCharset, this->macroEnabled,
          this->capitalizeMacro, std::move(macros),
          this->revertMacroOnBackspace)) {}

RewriteModeHandler::RewriteModeHandler(
    fcitx::EventLoop &eventLoop, StateFactory &stateFactory,
    InputScheduler &scheduler, BoolProvider autoCapitalizeProvider,
    BoolProvider debugProvider, BackendVerdictProtector backendVerdictProtector,
    BackspaceRecoveryProvider backspaceRecoveryProvider,
    ContextBoolProvider forwardFirstCharacterProvider,
    ContextBoolProvider restoreSurroundingTextProvider,
    MacroBypassMatcher macroBypassMatcher)
    : eventLoop_(eventLoop), stateFactory_(stateFactory), scheduler_(scheduler),
      autoCapitalizeProvider_(std::move(autoCapitalizeProvider)),
      debugProvider_(std::move(debugProvider)),
      backendVerdictProtector_(std::move(backendVerdictProtector)),
      backspaceRecoveryProvider_(std::move(backspaceRecoveryProvider)),
      forwardFirstCharacterProvider_(std::move(forwardFirstCharacterProvider)),
      restoreSurroundingTextProvider_(
          std::move(restoreSurroundingTextProvider)),
      macroBypassMatcher_(std::move(macroBypassMatcher)) {}

RewriteModeHandler::~RewriteModeHandler() {}

RewriteInputState *
RewriteModeHandler::stateFor(fcitx::InputContext &inputContext) const {
  return inputContext.propertyFor(&stateFactory_);
}

void RewriteModeHandler::activate(fcitx::InputContext &inputContext) {
  if (auto *state = stateFor(inputContext)) {
    // Focus có thể đi vào giữa một từ đã commit. Phím text đầu tiên sau focus
    // được phép đọc surrounding text một lần để phục hồi trạng thái Bamboo.
    state->surroundingRestoreArmed = true;
    state->knownTextBeforeCursor.clear();
  }
}

void RewriteModeHandler::onSurroundingTextUpdated(
    fcitx::InputContext &inputContext) {
  auto *state = stateFor(inputContext);
  // Khi Areca còn biết chắc text trước con trỏ thì update này chỉ phản ánh
  // chính lần commit vừa rồi, con trỏ không đổi chỗ. Arm ở đây khiến phím đầu
  // từ mới đọc snapshot, mà frontend trễ thì snapshot còn thiếu dấu cách vừa gõ
  // nên nạp nhầm từ trước vào từ mới.
  if (state && state->knownTextBeforeCursor.empty()) {
    state->surroundingRestoreArmed = true;
  }
}

void RewriteModeHandler::noteCommitted(fcitx::InputContext &inputContext,
                                       uint32_t deleteCount,
                                       const std::string &text) {
  if (auto *state = stateFor(inputContext)) {
    updateKnownTextBeforeCursor(state->knownTextBeforeCursor, deleteCount,
                                text);
  }
}

void RewriteModeHandler::tryRestoreFromSurroundingText(
    fcitx::InputContext &inputContext, RewriteInputState &state) {
  if (!state.surroundingRestoreArmed) {
    return;
  }
  state.surroundingRestoreArmed = false;

  const bool enabled = restoreSurroundingTextProvider_(inputContext);
  const bool supported =
      enabled && state.outputCharset == "Unicode" && state.engine &&
      state.engine->currentText().empty() && !scheduler_.rewritePending() &&
      scheduler_.queuedKeyCount() == 0 && !scheduler_.shouldRejectReset() &&
      !inputContext.capabilityFlags().test(fcitx::CapabilityFlag::Password) &&
      inputContext.capabilityFlags().test(
          fcitx::CapabilityFlag::SurroundingText);
  if (!supported) {
    if (debugProvider_() && enabled) {
      FCITX_INFO() << "areca: surrounding restore rejected"
                   << " reason=unsupported-state"
                   << " charset=" << state.outputCharset
                   << " pending=" << scheduler_.rewritePending()
                   << " queue=" << scheduler_.queuedKeyCount();
    }
    return;
  }

  const auto &surrounding = inputContext.surroundingText();
  if (!surrounding.isValid()) {
    if (debugProvider_())
      FCITX_INFO() << "areca: surrounding restore rejected reason=invalid";
    return;
  }
  // Snapshot phải chứa đủ text Areca đã commit trước con trỏ. Thiếu là frontend
  // còn gửi trễ; nạp theo snapshot đó sẽ lấy nhầm từ, nên bỏ qua lần này.
  if (!surroundingSnapshotEndsWith(true, surrounding.text(),
                                   surrounding.cursor(), surrounding.anchor(),
                                   state.knownTextBeforeCursor)) {
    if (debugProvider_()) {
      FCITX_INFO() << "areca: surrounding restore rejected"
                   << " reason=stale-snapshot text=" << surrounding.text()
                   << " cursor=" << surrounding.cursor()
                   << " known=" << state.knownTextBeforeCursor;
    }
    return;
  }

  auto candidate = extractSurroundingRestoreCandidate(
      surrounding.text(), surrounding.cursor(), surrounding.anchor());
  if (!candidate) {
    if (debugProvider_())
      FCITX_INFO() << "areca: surrounding restore rejected reason=no-word";
    return;
  }

  while (candidate && !state.engine->restoreFromRenderedText(candidate->text)) {
    candidate = shortenSurroundingRestoreCandidate(*candidate);
  }
  if (!candidate) {
    if (debugProvider_()) {
      FCITX_INFO() << "areca: surrounding restore rejected"
                   << " reason=bamboo-cannot-rebuild";
    }
    return;
  }

  if (debugProvider_()) {
    FCITX_INFO() << "areca: surrounding restore applied"
                 << " text=" << candidate->text
                 << " chars=" << candidate->characterCount;
  }
}

void RewriteModeHandler::deactivate(fcitx::InputContext &inputContext) {
  requestProtectedReset(inputContext);
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
  // Bắt đầu: Giữ snapshot Bamboo qua reset/deactivate của Edge X11
  if (syncEngineBackspace(state)) {
    state.resetProtectedUntilUsec = fcitx::now(CLOCK_MONOTONIC) + 20 * 1000;
  }
  // Kết thúc: Giữ snapshot Bamboo qua reset/deactivate của Edge X11
  state.backspaceReplay.forwarded(event.time(), event.rawKey().code(),
                                  fcitx::now(CLOCK_MONOTONIC));
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
                 << (capture ? "recovery" : "forward") << " reason="
                 << (capture ? "composition-active" : "gate-disabled")
                 << " current=" << currentText << " queue=" << queuedKeys
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
    const char *frontend = inputContext && inputContext->frontend()
                               ? inputContext->frontend()
                               : "";
    FCITX_INFO() << "areca: backspace recovery deferred until release"
                 << " current=" << state.engine->currentText()
                 << " queue=" << scheduler_.queuedKeyCount()
                 << " program=" << (inputContext ? inputContext->program() : "")
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
  const bool isRepeat = key.states().test(fcitx::KeyState::Repeat) ||
                        rawKey.states().test(fcitx::KeyState::Repeat);
  const bool isBackspace =
      key.check(FcitxKey_BackSpace) || rawKey.check(FcitxKey_BackSpace);
  const auto textSym = normalizeRewriteKeypadSym(normalizedKey.sym());
  const bool isEnter = textSym == FcitxKey_Return ||
                       rawSym == FcitxKey_Return || rawSym == FcitxKey_KP_Enter;

  if (debugProvider_() && (!event.isRelease() || isBackspace || isEnter)) {
    FCITX_INFO() << "areca: rewrite handler key=" << key.toString()
                 << " release=" << event.isRelease() << " repeat=" << isRepeat
                 << " pending=" << scheduler_.rewritePending()
                 << " queue=" << scheduler_.queuedKeyCount();
  }
  if (event.isRelease()) {
    if (isBackspace && state)
      state->backspaceReplay.clear();
    if (isBackspace && state && state->engine &&
        state->backspaceRecoveryAwaitingRelease) {
      if (isRepeat) {
        resetContext(*inputContext);
        event.forward();
      } else {
        runDeferredBackspaceRecovery(*inputContext, event, *state);
      }
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
  KnownTextGuard knownTextGuard(event, *state);

  const bool suppressMacro = !state->engine->currentText().empty() &&
                             macroBypassMatcher_(normalizedKey);

  if (isBackspace) {
    if (!scheduler_.shouldRejectReset()) {
      backendVerdictProtector_(*inputContext, "user-backspace");
    }
  } else if ((key.isCursorMove() || isSelectAllShortcut(rawKey)) &&
             !scheduler_.shouldRejectReset()) {
    backendVerdictProtector_(*inputContext, "selection-or-navigation-shortcut");
  }

  const bool resetAndForward =
      key.isCursorMove() || rawSym == FcitxKey_Tab ||
      rawSym == FcitxKey_KP_Tab || rawSym == FcitxKey_ISO_Left_Tab ||
      rawSym == FcitxKey_Escape ||
      (!suppressMacro && hasRewriteShortcutModifier(rawKey));
  if (resetAndForward) {
    if (scheduler_.shouldRejectReset()) {
      // Cursor/modifier keys injected by uinput during or within post-commit
      // window of a rewrite. Forward without resetting state.
      event.forward();
      return;
    }
    state->sentenceCapitalization.reset();
    state->engine->reset();
    event.forward();
    return;
  }
  if (textSym == FcitxKey_Delete || rawSym == FcitxKey_Delete) {
    if (scheduler_.shouldRejectReset()) {
      event.forward();
      return;
    }
    state->sentenceCapitalization.reset();
    event.forward();
    return;
  }
  if (isBackspace) {
    if (scheduler_.shouldRejectReset()) {
      // Backspace key injected by uinput during or within post-commit window of
      // a rewrite. Forward without running backspace recovery or resetting
      // state.
      event.forward();
      return;
    }
    state->sentenceCapitalization.reset();
    if (state->backspaceReplay.holding()) {
      const bool replay = state->backspaceReplay.takeReplay(
          event.time(), event.rawKey().code(), fcitx::now(CLOCK_MONOTONIC));
      if (replay) {
        if (debugProvider_()) {
          FCITX_INFO() << "areca: backspace route=forward reason=replayed-press"
                       << " event_time=" << event.time()
                       << " keycode=" << event.rawKey().code()
                       << " current=" << state->engine->currentText();
        }
        event.forward();
      } else {
        // A new held press stays forwarded and advances Bamboo exactly once.
        forwardSyncedBackspace(event, *state);
      }
      return;
    }
    if (state->backspaceRecoveryAwaitingRelease) {
      // IBus có thể gửi press lặp mà không có cờ Repeat. Press tiếp theo
      // trước release là nhấn giữ; phím inject đã được loại ở các guard trên.
      const char *frontend = inputContext->frontend();
      const bool ibusHeldPress =
          frontend && std::string_view(frontend) == "ibus";
      if (isRepeat || ibusHeldPress) {
        if (debugProvider_()) {
          FCITX_INFO() << "areca: backspace route=forward"
                       << " reason=repeat-reset-composition"
                       << " repeat=" << isRepeat
                       << " inferred_ibus_repeat=" << ibusHeldPress
                       << " program=" << inputContext->program();
        }
        resetContext(*inputContext);
        state->backspaceReplay.forwarded(event.time(), event.rawKey().code(),
                                         fcitx::now(CLOCK_MONOTONIC));
        event.forward();
        return;
      }
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
    if (state->engine && state->engine->canUndoMacro()) {
      if (debugProvider_()) {
        FCITX_INFO() << "areca: backspace route=macro-undo"
                     << " queue=" << scheduler_.queuedKeyCount()
                     << " program=" << inputContext->program();
      }
      // Bắt đầu: Macro undo chặn auto-repeat và chỉ chạy sau khi thả Backspace
      deferBackspaceRecoveryUntilRelease(event, *state);
      // Kết thúc: Macro undo chặn auto-repeat và chỉ chạy sau khi thả Backspace
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
  // Chỉ phục hồi nếu chính kiểu gõ Bamboo hiện tại xử lý phím này. Vì vậy Space
  // và dấu câu kết thúc macro không hút snapshot cũ vào engine, trong khi số
  // của VNI hoặc ký hiệu của VIQR/Telex 2 vẫn có thể sửa từ đã commit.
  const bool canProcessText = state->engine->canProcessKey(codepoint);
  if (canProcessText) {
    if (state->wordBoundaryPending) {
      ++state->wordSerial;
      state->wordBoundaryPending = false;
    }
    tryRestoreFromSurroundingText(*inputContext, *state);
  } else {
    state->surroundingRestoreArmed = false;
    state->wordBoundaryPending = true;
  }
  // Thử cho phím đầu đi tiếp trước khi chặn event để editor web có thể mở chế
  // độ soạn thảo. Không chen vào Backspace đang chờ release; nếu tự viết hoa đã
  // đổi ký tự thì phải commit ký tự mới, vì phím gốc vẫn mang ký tự chưa đổi.
  // Đọc cấu hình hiện tại mỗi lần để bật/tắt có hiệu lực mà không tạo lại
  // handler.
  if (forwardFirstCharacterProvider_(*inputContext) &&
      !state->backspaceRecoveryAwaitingRelease && effectiveTextSym == textSym &&
      scheduler_.handleIdleKey(event, codepoint, utf8Text)) {
    if (!event.filtered()) {
      knownTextGuard.keep();
    }
    return;
  }
  event.filterAndAccept();
  if (suppressMacro && debugProvider_()) {
    FCITX_INFO() << "areca: rewrite macro expansion bypassed";
  }
  scheduler_.enqueue(*inputContext, codepoint, utf8Text, suppressMacro);
}

void RewriteModeHandler::requestProtectedReset(
    fcitx::InputContext &inputContext) {
  // Bắt đầu: Giữ snapshot Bamboo qua reset/deactivate của Edge X11
  if (auto *state = stateFor(inputContext);
      state && fcitx::now(CLOCK_MONOTONIC) <= state->resetProtectedUntilUsec) {
    if (debugProvider_()) {
      FCITX_INFO() << "areca: reset rejected"
                   << " reason=post-backspace-window";
    }
    return;
  }
  // Kết thúc: Giữ snapshot Bamboo qua reset/deactivate của Edge X11
  if (scheduler_.shouldRejectReset()) {
    if (debugProvider_()) {
      FCITX_INFO() << "areca: reset rejected (active rewrite or 50ms "
                      "post-commit window)";
    }
    return;
  }
  resetContext(inputContext);
}

void RewriteModeHandler::resetContext(fcitx::InputContext &inputContext) {
  scheduler_.resetContext(inputContext);
  if (auto *state = stateFor(inputContext)) {
    state->backspaceRecoveryAwaitingRelease = false;
    state->backspaceReplay.clear();
    state->resetProtectedUntilUsec = 0;
    state->sentenceCapitalization.reset();
    // Sau reset, composition cũ đã mất. Arm lại để phím text kế tiếp có thể tái
    // tạo trạng thái từ vị trí con trỏ mới nếu người dùng bật tính năng.
    state->surroundingRestoreArmed = true;
    state->knownTextBeforeCursor.clear();
    state->wordSerial = 0;
    state->wordBoundaryPending = false;
  }
}

} // namespace areca
