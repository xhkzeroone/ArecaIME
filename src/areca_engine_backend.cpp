#include "areca_engine.h"

#include <algorithm>
#include <array>

#include "browser_autocomplete.h"
#include "program_compatibility.h"
#include "surrounding_probe_match.h"
#include <fcitx-utils/log.h>
#include <fcitx/inputcontext.h>
#include <fcitx/surroundingtext.h>

#include "mouse_click_tracker.h"
#include "window_focus_tracker.h"

namespace areca {

namespace {

constexpr uint64_t kBackendVerdictProtectionUsec = 1ULL * 1000 * 1000;

using SurroundingRule = bool (*)(const fcitx::SurroundingText &);

constexpr std::array<SurroundingRule, 3> kInvalidSurroundingRules = {
    [](const auto &s) { return s.cursor() <= 0; },
    [](const auto &s) { return s.anchor() <= 0; },
    [](const auto &s) { return s.text().empty(); }};

bool isInvalidSurrounding(const fcitx::SurroundingText &surrounding) {
  return std::any_of(kInvalidSurroundingRules.begin(),
                     kInvalidSurroundingRules.end(),
                     [&](auto rule) { return rule(surrounding); });
}

bool isGnomeShellProxyProgram(std::string_view program) {
  return program == "gnome-shell" || program == "mutter" ||
         program == "org.gnome.Shell" || program == "org.gnome.Mutter";
}

} // namespace

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

bool ArecaEngine::surroundingBackendInUse(fcitx::InputContext &inputContext) {
  const auto program = resolveProgram(
      inputContext, inputContext.propertyFor(&rewriteStateFactory_));
  const auto it = surroundingBackendByApp_.find(program);
  // Chưa từng chọn backend cho app này thì chưa có lý do để tắt restore.
  return it == surroundingBackendByApp_.end() || it->second;
}

void ArecaEngine::noteRewriteBackendChoice(fcitx::InputContext &inputContext,
                                           const RewriteBackend *backend) {
  const auto program = resolveProgram(
      inputContext, inputContext.propertyFor(&rewriteStateFactory_));
  if (program.empty()) {
    return;
  }
  surroundingBackendByApp_[program] =
      backend == &surroundingBackend_ || backend == &surroundingV2Backend_;
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
  if (std::string_view(eventName) != "reset" &&
      backendVerdictProtected(inputContext)) {
    if (debugEnabled()) {
      FCITX_INFO() << "areca: backend verdict cache preserved"
                   << " event=" << eventName
                   << " program=" << inputContext.program();
    }
    return;
  }
  backendVerdict_.reset();
  backendVerdictProtectedUntil_ = 0;
  if (std::string_view(eventName) == "reset") {
    const std::string prog = resolveProgram(
        inputContext, inputContext.propertyFor(&rewriteStateFactory_));
    if (!prog.empty()) {
      lastAutoBackendByApp_.erase(prog);
    }
  }
  if (debugEnabled()) {
    FCITX_INFO() << "areca: backend verdict cache cleared"
                 << " event=" << eventName
                 << " program=" << inputContext.program();
  }
}

std::string ArecaEngine::resolveProgram(fcitx::InputContext &inputContext,
                                        RewriteInputState *state) {
  const std::string reportedProgram = inputContext.program();
  const bool needsTrackedProgram =
      reportedProgram.empty() || isGnomeShellProxyProgram(reportedProgram);
  if (!needsTrackedProgram) {
    return reportedProgram;
  }

  if (focusTracker_ && focusTracker_->isValid()) {
    const std::string trackedProgram = focusTracker_->focusProgram();
    if (!trackedProgram.empty() && !isGnomeShellProxyProgram(trackedProgram)) {
      if (state) {
        state->resolvedProgram = trackedProgram;
      }
      if (debugEnabled()) {
        FCITX_INFO()
            << "areca: resolved and cached proxy program via focus tracker"
            << " reported=" << reportedProgram << " tracked=" << trackedProgram;
      }
      return trackedProgram;
    }
  }

  if (state && !state->resolvedProgram.empty()) {
    return state->resolvedProgram;
  }
  return reportedProgram;
}

RewriteBackend *ArecaEngine::resolvePreferredSurroundingBackend() {
  if (advancedConfig_.useSurroundingV2InsteadOfSurroundingV1.value()) {
    return &surroundingV2Backend_;
  }
  return &surroundingBackend_;
}

void ArecaEngine::handleSurroundingProbeSucceeded(
    fcitx::InputContext &inputContext) {
  backendVerdict_.probePending = false;
  backendVerdict_.retryOnNextWord = false;
  if (debugEnabled()) {
    FCITX_INFO() << "areca: surrounding probe succeeded program="
                 << inputContext.program();
  }
}

void ArecaEngine::handleSurroundingProbeFailed(
    fcitx::InputContext &inputContext) {
  auto *state = inputContext.propertyFor(&rewriteStateFactory_);
  const uint32_t wordSerial = state ? state->wordSerial : 0;
  backendVerdict_.probeFailed(wordSerial);
  // Transaction này đã hoàn tất bằng backend dự phòng.
  noteRewriteBackendChoice(inputContext, nullptr);
  if (debugEnabled()) {
    FCITX_INFO() << "areca: surrounding probe failed, marked unreliable"
                 << " attempt=" << backendVerdict_.probeAttempts
                 << " retry_next_word=" << backendVerdict_.retryOnNextWord
                 << " word=" << wordSerial
                 << " program=" << inputContext.program();
  }
}

RewriteBackend *ArecaEngine::resolveSurroundingFallbackBackend(
    fcitx::InputContext &inputContext) {
  const std::string prog = resolveProgram(
      inputContext, inputContext.propertyFor(&rewriteStateFactory_));
  if (config_.shiftSelectFallbackForBrowser.value() &&
      inputTypeDetector_.isBrowser(prog)) {
    if (auto *shiftBackend = resolvePreferredShiftSelectBackend()) {
      return shiftBackend;
    }
  }
  const char *frontend = inputContext.frontend();
  const bool isTerminal = inputTypeDetector_.isTerminal(prog, frontend);
  const bool canForward =
      inputTypeDetector_.canForwardBackspaces(inputContext, prog);
  return resolvePreferredBackspaceBackend(isTerminal || !canForward);
}

RewriteBackend *ArecaEngine::resolvePreferredShiftSelectBackend() {
  if (advancedConfig_.useXTestInsteadOfUinput.value() &&
      xtestShiftSelectBackend_.isAvailable()) {
    return &xtestShiftSelectBackend_;
  }
  if (uinputShiftSelectBackend_.isAvailable()) {
    return &uinputShiftSelectBackend_;
  }
  if (xtestShiftSelectBackend_.isAvailable()) {
    return &xtestShiftSelectBackend_;
  }
  return nullptr;
}

RewriteBackend *
ArecaEngine::resolvePreferredBackspaceBackend(bool preferHardware) {
  if (preferHardware || advancedConfig_.useUinputInsteadOfForwardKey.value()) {
    if (advancedConfig_.useXTestInsteadOfUinput.value() &&
        xtestBackspaceBackend_.isAvailable()) {
      return &xtestBackspaceBackend_;
    }
    // Phím ảo Wayland xoá và gõ chữ mới trên cùng một kênh; chỉ có khi
    // compositor cấp zwp_virtual_keyboard_v1, không thì vẫn dùng uinput.
    if (advancedConfig_.useVirtualKeyboardInsteadOfUinput.value() &&
        virtualKeyboardBackend_.isAvailable()) {
      return &virtualKeyboardBackend_;
    }
    if (uinputBackspaceBackend_.isAvailable()) {
      return &uinputBackspaceBackend_;
    }
    if (xtestBackspaceBackend_.isAvailable()) {
      return &xtestBackspaceBackend_;
    }
  }
  return &forwardBackspaceBackend_;
}

std::string ArecaEngine::describeAutoBackend(const std::string &appName) {
  auto it = lastAutoBackendByApp_.find(appName);
  if (it != lastAutoBackendByApp_.end()) {
    switch (it->second) {
    case AppBackendMode::SurroundingText:
      return "Tự động (Surrounding)";
    case AppBackendMode::ShiftSelect:
      return "Tự động (Shift select)";
    case AppBackendMode::Backspace:
      return "Tự động (Backspace)";
    case AppBackendMode::Preedit:
    case AppBackendMode::Redirect:
      break;
    default:
      break;
    }
  }
  return "Tự động";
}

std::string ArecaEngine::describeSurroundingBackend() {
  if (advancedConfig_.useSurroundingV2InsteadOfSurroundingV1.value()) {
    return "V2";
  }
  return "V1";
}

std::string ArecaEngine::describeShiftSelectBackend() {
  if (auto *shiftBackend = resolvePreferredShiftSelectBackend()) {
    if (std::string_view(shiftBackend->name()).starts_with("native")) {
      return "Native";
    }
    if (std::string_view(shiftBackend->name()).starts_with("uinput")) {
      return "Uinput";
    }
  }
  return advancedConfig_.useXTestInsteadOfUinput.value() ? "Native" : "Uinput";
}

std::string ArecaEngine::describeBackspaceBackend(bool preferHardware) {
  if (auto *bsBackend = resolvePreferredBackspaceBackend(preferHardware)) {
    const std::string_view name = bsBackend->name();
    if (name.starts_with("native")) {
      return "Native";
    }
    if (name.starts_with("uinput")) {
      return "Uinput";
    }
    if (name == "virtual-keyboard") {
      return "Virtual keyboard";
    }
  }
  return "ForwardKey";
}

RewriteBackendSelection
ArecaEngine::selectRewriteBackend(fcitx::InputContext &inputContext,
                                  const BambooResult &result) {
  auto *state = inputContext.propertyFor(&rewriteStateFactory_);
  if (!state) {
    return {resolvePreferredBackspaceBackend(false)};
  }

  const std::string program = resolveProgram(inputContext, state);

  const char *frontend = inputContext.frontend();
  const bool isTerminal = inputTypeDetector_.isTerminal(program, frontend);
  const bool canForward =
      inputTypeDetector_.canForwardBackspaces(inputContext, program);
  const bool preferHardware = isTerminal || !canForward;

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

  if (isTerminal) {
    RewriteBackend *backend = resolvePreferredBackspaceBackend(true);
    if (!program.empty()) {
      lastAutoBackendByApp_[program] = AppBackendMode::Backspace;
    }
    if (debugEnabled()) {
      FCITX_INFO() << "areca: terminal selected backend=" << backend->name()
                   << " program=" << program;
    }
    return {backend};
  }

  const auto capabilities = inputContext.capabilityFlags();
  const auto &surrounding = inputContext.surroundingText();
  const bool hasSurrounding =
      capabilities.test(fcitx::CapabilityFlag::SurroundingText) &&
      surrounding.isValid();
  // Bắt đầu: Nhận diện thanh địa chỉ Chromium khi SurroundingText không khả
  // dụng hoặc không hợp lệ
  const bool inAddressBar =
      !hasSurrounding &&
      inputTypeDetector_.inChromiumAddressBar(inputContext, program);
  // Kết thúc: Nhận diện thanh địa chỉ Chromium khi SurroundingText không khả
  // dụng hoặc không hợp lệ
  const bool isUrl =
      capabilities.test(fcitx::CapabilityFlag::Url) || inAddressBar;
  const bool forceUrlBackspace = shouldForceBackspaceForUrl(
      config_.useBackspaceForUrl.value(), capabilities, inAddressBar);
  const auto decision =
      evaluateReliability(inputContext, result.currentText, program);
  const bool hasActiveSelection =
      surrounding.isValid() && surrounding.cursor() != surrounding.anchor();
  const bool selectionMatchesCurrentText =
      hasActiveSelection &&
      isSelectionImmediatelyAfterText(surrounding.text(), surrounding.cursor(),
                                      surrounding.anchor(), result.currentText);
  const bool collapsedFirefoxAutocomplete =
      surrounding.isValid() &&
      isCollapsedFirefoxAutocomplete(program, surrounding.text(),
                                     surrounding.cursor(), surrounding.anchor(),
                                     result.currentText);

  // Chỉ xử lý autocomplete đặc biệt khi frontend cung cấp surrounding text
  // đủ để quan sát selection. Không đoán ký tự autocomplete khi surrounding
  // không khả dụng.
  if (decision.browserAutocomplete || collapsedFirefoxAutocomplete ||
      hasActiveSelection) {
    if (isUrl) {
      auto *shiftBackend = resolvePreferredShiftSelectBackend();
      if (shiftBackend) {
        if (!program.empty()) {
          lastAutoBackendByApp_[program] = AppBackendMode::ShiftSelect;
        }
        if (debugEnabled()) {
          FCITX_INFO() << "areca: autocomplete selected shift-select backend="
                       << shiftBackend->name() << " collapsed_autocomplete="
                       << collapsedFirefoxAutocomplete
                       << " active_selection=" << hasActiveSelection
                       << " bamboo_delete=" << result.deleteCount
                       << " program=" << program;
        }
        return {shiftBackend};
      }
    }

    uint32_t additional = 0;
    if (selectionMatchesCurrentText || decision.browserAutocomplete ||
        collapsedFirefoxAutocomplete) {
      additional = 1;
    }
    RewriteBackend *backend = resolvePreferredBackspaceBackend(preferHardware);
    if (!program.empty()) {
      lastAutoBackendByApp_[program] = AppBackendMode::Backspace;
    }
    if (debugEnabled()) {
      FCITX_INFO() << "areca: browser autocomplete or address bar strategy="
                   << backend->name() << " is_url=" << isUrl
                   << " in_address_bar=" << inAddressBar
                   << " collapsed_autocomplete=" << collapsedFirefoxAutocomplete
                   << " active_selection=" << hasActiveSelection
                   << " selection_matches_current="
                   << selectionMatchesCurrentText
                   << " additional_backspaces=" << additional
                   << " bamboo_delete=" << result.deleteCount;
    }
    return {backend, additional};
  }

  // URL/address bar không có surrounding dùng Shift+Select để thay đúng phần
  // Bamboo yêu cầu. Nếu backend này không khả dụng mới lùi về Backspace.
  if (shouldUseShiftSelectForUrl(isUrl, hasSurrounding,
                                 inputTypeDetector_.isBrowser(program))) {
    if (auto *shiftBackend = resolvePreferredShiftSelectBackend()) {
      if (!program.empty()) {
        lastAutoBackendByApp_[program] = AppBackendMode::ShiftSelect;
      }
      if (debugEnabled()) {
        FCITX_INFO() << "areca: url without surrounding selected shift-select "
                        "backend="
                     << shiftBackend->name() << " is_url=" << isUrl
                     << " in_address_bar=" << inAddressBar
                     << " program=" << program;
      }
      return {shiftBackend};
    }
  }

  // Khi cấu hình buộc xử lý URL, ưu tiên Shift+Select kể cả khi surrounding
  // khả dụng. Chỉ lùi về Backspace nếu không có Shift+Select backend.
  if (forceUrlBackspace) {
    if (auto *shiftBackend = resolvePreferredShiftSelectBackend()) {
      if (!program.empty()) {
        lastAutoBackendByApp_[program] = AppBackendMode::ShiftSelect;
      }
      if (debugEnabled()) {
        FCITX_INFO() << "areca: url selected shift-select backend="
                     << shiftBackend->name()
                     << " has_surrounding=" << hasSurrounding
                     << " program=" << program;
      }
      return {shiftBackend};
    }

    RewriteBackend *backend = resolvePreferredBackspaceBackend(preferHardware);
    if (!program.empty()) {
      lastAutoBackendByApp_[program] = AppBackendMode::Backspace;
    }
    if (debugEnabled()) {
      FCITX_INFO() << "areca: url shift-select unavailable, fallback backend="
                   << backend->name() << " has_surrounding=" << hasSurrounding
                   << " program=" << program;
    }
    return {backend};
  }

  if (!program.empty()) {
    auto it = appBackendOverridesMap_.find(program);
    if (it != appBackendOverridesMap_.end() &&
        it->second != AppBackendMode::Auto) {
      switch (it->second) {
      case AppBackendMode::SurroundingText:
        if (decision.useSurrounding) {
          auto *surBackend = resolvePreferredSurroundingBackend();
          if (backendVerdict_.probePending) {
            backendVerdict_.probeStarted();
          }
          const uint32_t probeTimeoutMs =
              backendVerdict_.probePending
                  ? backendVerdict_.activeProbeTimeoutMs()
                  : 0;
          if (debugEnabled()) {
            FCITX_INFO() << "areca: app-override selected surrounding backend="
                         << surBackend->name() << " program=" << program
                         << " probe_timeout_ms=" << probeTimeoutMs;
          }
          return {surBackend, 0, backendVerdict_.probePending, probeTimeoutMs};
        }
        if (config_.shiftSelectFallbackForBrowser.value() && !isTerminal &&
            inputTypeDetector_.isBrowser(program)) {
          if (auto *shiftBackend = resolvePreferredShiftSelectBackend()) {
            if (debugEnabled()) {
              FCITX_INFO() << "areca: browser surrounding disabled or failed, "
                              "fallback to shift-select"
                           << " program=" << program
                           << " frontend=" << (frontend ? frontend : "")
                           << " backend=" << shiftBackend->name();
            }
            return {shiftBackend};
          }
        }
        return {resolvePreferredBackspaceBackend(preferHardware)};
      case AppBackendMode::ShiftSelect:
        if (!isTerminal) {
          if (auto *shiftBackend = resolvePreferredShiftSelectBackend()) {
            if (debugEnabled()) {
              FCITX_INFO()
                  << "areca: app-override selected shift-select backend="
                  << shiftBackend->name() << " program=" << program;
            }
            return {shiftBackend};
          }
        }
        return {resolvePreferredBackspaceBackend(preferHardware)};
      case AppBackendMode::Backspace: {
        auto *bsBackend = resolvePreferredBackspaceBackend(preferHardware);
        if (debugEnabled()) {
          FCITX_INFO() << "areca: app-override selected backspace backend="
                       << bsBackend->name() << " program=" << program;
        }
        return {bsBackend};
      }
      case AppBackendMode::VirtualKeyboard: {
        if (virtualKeyboardBackend_.isAvailable()) {
          if (debugEnabled()) {
            FCITX_INFO() << "areca: app-override selected virtual-keyboard"
                         << " program=" << program;
          }
          return {&virtualKeyboardBackend_};
        }
        // Compositor không cấp phím ảo (KWin, GNOME): dùng như khi chọn
        // Backspace.
        auto *bsBackend = resolvePreferredBackspaceBackend(preferHardware);
        if (debugEnabled()) {
          FCITX_INFO() << "areca: virtual-keyboard unavailable, fallback "
                       << "backend=" << bsBackend->name()
                       << " program=" << program;
        }
        return {bsBackend};
      }
      case AppBackendMode::Preedit:
      case AppBackendMode::Redirect:
        // Mode routing happens before key handling, so these entries must not
        // normally reach the rewrite scheduler. Fall through to auto as a
        // safe fallback if an already queued transaction outlives a switch.
        break;
      case AppBackendMode::Auto:
        break;
      }
    }
  }

  if (decision.useSurrounding) {
    auto *surBackend = resolvePreferredSurroundingBackend();
    if (backendVerdict_.probePending) {
      backendVerdict_.probeStarted();
    }
    const uint32_t probeTimeoutMs = backendVerdict_.probePending
                                        ? backendVerdict_.activeProbeTimeoutMs()
                                        : 0;
    if (!program.empty()) {
      lastAutoBackendByApp_[program] = AppBackendMode::SurroundingText;
    }
    if (debugEnabled()) {
      FCITX_INFO() << "areca: auto selected surrounding backend="
                   << surBackend->name() << " program=" << program
                   << " probe=" << backendVerdict_.probePending
                   << " probe_timeout_ms=" << probeTimeoutMs;
    }
    return {surBackend, 0, backendVerdict_.probePending, probeTimeoutMs};
  }

  if (config_.shiftSelectFallbackForBrowser.value() && !isTerminal &&
      inputTypeDetector_.isBrowser(program)) {
    if (auto *shiftBackend = resolvePreferredShiftSelectBackend()) {
      if (!program.empty()) {
        lastAutoBackendByApp_[program] = AppBackendMode::ShiftSelect;
      }
      if (debugEnabled()) {
        FCITX_INFO() << "areca: browser surrounding disabled or failed, "
                        "fallback to shift-select"
                     << " program=" << program
                     << " frontend=" << (frontend ? frontend : "")
                     << " backend=" << shiftBackend->name();
      }
      return {shiftBackend};
    }
  }

  if (!program.empty()) {
    lastAutoBackendByApp_[program] = AppBackendMode::Backspace;
  }
  return {resolvePreferredBackspaceBackend(preferHardware)};
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

  // Bắt đầu: Probe lại đúng một lần khi bước sang từ kế tiếp
  auto *state = inputContext.propertyFor(&rewriteStateFactory_);
  if (!browserAutocomplete && state &&
      backendVerdict_.retryForWord(state->wordSerial) && debugEnabled()) {
    FCITX_INFO() << "areca: surrounding probe retry armed"
                 << " attempt=" << (backendVerdict_.probeAttempts + 1)
                 << " word=" << state->wordSerial << " program=" << programName;
  }
  // Kết thúc: Probe lại đúng một lần khi bước sang từ kế tiếp

  if (!backendVerdict_.known && !browserAutocomplete) {
    backendVerdict_.reliable = !isInvalidSurrounding(surrounding);
    backendVerdict_.probePending = backendVerdict_.reliable;
    if (debugEnabled()) {
      FCITX_INFO() << "areca: reliability first-probe text="
                   << surrounding.text() << " cursor=" << surrounding.cursor()
                   << " anchor=" << surrounding.anchor()
                   << " reliable=" << backendVerdict_.reliable
                   << " probe_pending=" << backendVerdict_.probePending;
    }

    backendVerdict_.forceForwardBackspace = false;
    if (isProgrammingProgram(programName) || isTerminalProgram(programName) ||
        isOfficeProgram(programName)) {
      backendVerdict_.forceForwardBackspace = true;
      if (debugEnabled()) {
        FCITX_INFO() << "areca: reliability first-probe force_forward=1"
                     << " reason=programming-terminal-or-office-program"
                     << " program=" << programName;
      }
    } else if (backendVerdict_.reliable && isVSCodeFamilyProgram(programName)) {
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

  // Lúc thăm dò, snapshot phải chứa từ đang hiện trong ô. Frontend trễ một nhịp
  // (báo "g" khi ô đã là "go") khiến probe tính sai kết quả mong đợi rồi nhận
  // nhầm update cũ là xác nhận xoá. Khi đó coi probe thất bại: dùng
  // Backspace/Shift+Left và thử lại ở từ kế tiếp.
  const bool staleDuringProbe =
      !browserAutocomplete && backendVerdict_.probePending &&
      backendVerdict_.reliable &&
      surrounding.cursor() == surrounding.anchor() &&
      !surroundingSnapshotEndsWith(surrounding.isValid(), surrounding.text(),
                                   surrounding.cursor(), surrounding.anchor(),
                                   shownText);
  if (staleDuringProbe) {
    // Tính là một lần probe để chỉ retry đúng một lần như probe timeout.
    backendVerdict_.probeStarted();
    backendVerdict_.probeFailed(state ? state->wordSerial : 0);
    if (debugEnabled()) {
      FCITX_INFO() << "areca: surrounding snapshot stale during probe text="
                   << surrounding.text() << " cursor=" << surrounding.cursor()
                   << " anchor=" << surrounding.anchor()
                   << " word=" << shownText << " marked_unreliable=1"
                   << " attempt=" << backendVerdict_.probeAttempts
                   << " retry_next_word=" << backendVerdict_.retryOnNextWord
                   << " program=" << programName;
    }
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

} // namespace areca
