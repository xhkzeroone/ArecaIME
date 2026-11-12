#include "areca_engine.h"

#include <functional>
#include <utility>

#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/userinterfacemanager.h>

namespace areca {

namespace {

std::string icUuidToHex(const fcitx::ICUUID &uuid) {
  static constexpr char hexChars[] = "0123456789abcdef";
  std::string result;
  result.reserve(32);
  for (uint8_t byte : uuid) {
    result.push_back(hexChars[(byte >> 4) & 0x0f]);
    result.push_back(hexChars[byte & 0x0f]);
  }
  return result;
}

class BackendCandidateWord : public fcitx::CandidateWord {
public:
  BackendCandidateWord(fcitx::Text text, AppBackendMode mode,
                       std::function<void(AppBackendMode)> onSelect)
      : fcitx::CandidateWord(std::move(text)), mode_(mode),
        onSelect_(std::move(onSelect)) {}

  void select(fcitx::InputContext *) const override {
    if (onSelect_) {
      onSelect_(mode_);
    }
  }

  AppBackendMode mode() const { return mode_; }

private:
  AppBackendMode mode_;
  std::function<void(AppBackendMode)> onSelect_;
};

} // namespace

bool ArecaEngine::isBackendSelecting(fcitx::InputContext &inputContext) const {
  auto *candList = inputContext.inputPanel().candidateList().get();
  auto *commonList = dynamic_cast<fcitx::CommonCandidateList *>(candList);
  return commonList && commonList->size() > 0 &&
         dynamic_cast<const BackendCandidateWord *>(
             &commonList->candidate(0)) != nullptr;
}

void ArecaEngine::showBackendSelectionMenu(fcitx::InputContext &inputContext) {
  auto *rewriteState = inputContext.propertyFor(&rewriteStateFactory_);
  std::string appName = resolveProgram(inputContext, rewriteState);
  if (appName.empty()) {
    appName = "context-" + icUuidToHex(inputContext.uuid()).substr(0, 8);
    if (rewriteState) {
      rewriteState->resolvedProgram = appName;
    }
  }

  auto currentIt = appBackendOverridesMap_.find(appName);
  const AppBackendMode currentMode =
      (currentIt != appBackendOverridesMap_.end()) ? currentIt->second
                                                   : AppBackendMode::Auto;

  auto candList = std::make_unique<fcitx::CommonCandidateList>();
  candList->setLayoutHint(fcitx::CandidateLayoutHint::Vertical);
  candList->setLabels({"1. ", "2. ", "3. ", "4. ", "5. ", "6. ", "A. ", "E. "});
  candList->setPageSize(8);

  const char *frontend = inputContext.frontend();
  const bool isTerminal = inputTypeDetector_.isTerminal(appName, frontend);
  const bool canForward =
      inputTypeDetector_.canForwardBackspaces(inputContext, appName);
  const bool preferHardware = isTerminal || !canForward;

  const std::string autoTitle =
      activePresentationMode_ == PresentationMode::Rewrite
          ? describeAutoBackend(appName)
          : "Mặc định (" +
                std::string(presentationModeName(activePresentationMode_)) +
                ")";
  const std::string surroundingTitle =
      "Surrounding (" + describeSurroundingBackend() + ")";
  const std::string shiftTitle =
      "Shift select (" + describeShiftSelectBackend() + ")";
  const std::string backspaceTitle =
      "Backspace (" + describeBackspaceBackend(preferHardware) + ")";
  const std::string virtualKeyboardTitle =
      std::string("Virtual keyboard (") +
      (virtualKeyboardBackend_.isAvailable() ? "Wayland" : "không khả dụng") +
      ")";

  struct BackendOption {
    AppBackendMode mode;
    std::string title;
  };
  const std::array<BackendOption, 8> options = {{
      {AppBackendMode::Auto, autoTitle},
      {AppBackendMode::SurroundingText, surroundingTitle},
      {AppBackendMode::ShiftSelect, shiftTitle},
      {AppBackendMode::Backspace, backspaceTitle},
      {AppBackendMode::VirtualKeyboard, virtualKeyboardTitle},
      // Không hỏi isAvailable() ở đây: lần đầu sẽ mở hộp thoại xin quyền libei.
      {AppBackendMode::Native, "Native (libei)"},
      {AppBackendMode::Preedit, "Preedit"},
      {AppBackendMode::Redirect, "Redirect (EN)"},
  }};

  int initialCursor = 0;
  for (size_t i = 0; i < options.size(); ++i) {
    std::string label = options[i].title;
    if (options[i].mode == currentMode) {
      label += "  [Hiện tại]";
      initialCursor = static_cast<int>(i);
    }
    candList->append(std::make_unique<BackendCandidateWord>(
        fcitx::Text(label), options[i].mode,
        [this, &inputContext, appName](AppBackendMode mode) {
          auto inputContextWatch = inputContext.watch();
          backendSelectionPost_.schedule(
              [this, inputContextWatch, appName, mode]() mutable {
                if (auto *ic = inputContextWatch.get()) {
                  selectBackendForApp(*ic, appName, mode);
                }
              });
        }));
  }

  candList->setGlobalCursorIndex(initialCursor);

  auto &panel = inputContext.inputPanel();
  panel.reset();
  panel.setAuxUp(fcitx::Text("Chọn chế độ / backend cho: " + appName));
  panel.setCandidateList(std::move(candList));
  inputContext.updateUserInterface(fcitx::UserInterfaceComponent::InputPanel,
                                   true);
}

void ArecaEngine::selectBackendForApp(fcitx::InputContext &inputContext,
                                      std::string appName,
                                      AppBackendMode mode) {
  const PresentationMode previousPresentationMode =
      effectivePresentationMode(inputContext);

  auto &overrides = *appProfiles_.appOverrides.mutableValue();
  auto it = std::find_if(overrides.begin(), overrides.end(),
                         [&appName](const AppBackendOverrideEntry &entry) {
                           return entry.appName.value() == appName;
                         });

  if (it != overrides.end()) {
    it->mode.setValue(mode);
    const char *frontend = inputContext.frontend();
    it->frontend.setValue(frontend ? frontend : "");
  } else {
    AppBackendOverrideEntry entry;
    entry.appName.setValue(appName);
    const char *frontend = inputContext.frontend();
    entry.frontend.setValue(frontend ? frontend : "");
    entry.mode.setValue(mode);
    overrides.push_back(std::move(entry));
  }

  applyConfig();
  save();

  const PresentationMode nextPresentationMode =
      effectivePresentationMode(inputContext);
  if (previousPresentationMode != nextPresentationMode) {
    clearBackendVerdictForLifecycle(inputContext, "app-mode-switch");
    handlerFor(inputContext).activate(inputContext);
  }

  inputContext.inputPanel().reset();
  inputContext.updateUserInterface(fcitx::UserInterfaceComponent::InputPanel,
                                   true);

  const char *frontend = inputContext.frontend();
  const bool isTerminal = inputTypeDetector_.isTerminal(appName, frontend);
  const bool canForward =
      inputTypeDetector_.canForwardBackspaces(inputContext, appName);
  const bool preferHardware = isTerminal || !canForward;

  std::string modeDesc;
  switch (mode) {
  case AppBackendMode::Auto:
    modeDesc = describeAutoBackend(appName);
    break;
  case AppBackendMode::SurroundingText:
    modeDesc = "Surrounding (" + describeSurroundingBackend() + ")";
    break;
  case AppBackendMode::ShiftSelect:
    modeDesc = "Shift select (" + describeShiftSelectBackend() + ")";
    break;
  case AppBackendMode::Backspace:
    modeDesc = "Backspace (" + describeBackspaceBackend(preferHardware) + ")";
    break;
  case AppBackendMode::Preedit:
    modeDesc = "Preedit";
    break;
  case AppBackendMode::Redirect:
    modeDesc = "Redirect (EN)";
    break;
  case AppBackendMode::VirtualKeyboard:
    modeDesc = virtualKeyboardBackend_.isAvailable()
                   ? "Virtual keyboard (Wayland)"
                   : "Virtual keyboard (không khả dụng, dùng Backspace)";
    break;
  case AppBackendMode::Native:
    modeDesc = "Native (libei)";
    break;
  }

  const std::string infoMsg = appName + " -> " + modeDesc;
  updateUI(&inputContext);
#ifdef ARECA_HAVE_SHOW_CUSTOM_INFO
  instance_->showCustomInputMethodInformation(&inputContext, infoMsg);
#else
  instance_->showInputMethodInformation(&inputContext);
#endif
}

bool ArecaEngine::handleBackendSelectionKey(fcitx::InputContext &inputContext,
                                            fcitx::KeyEvent &event) {
  if (event.isRelease()) {
    return true;
  }

  auto *candList = inputContext.inputPanel().candidateList().get();
  auto *commonList = dynamic_cast<fcitx::CommonCandidateList *>(candList);
  if (!commonList) {
    return false;
  }

  const auto key = event.key().normalize();
  const auto sym = key.sym();

  if (key.checkKeyList(config_.selectBackendKey.value())) {
    inputContext.inputPanel().reset();
    inputContext.updateUserInterface(fcitx::UserInterfaceComponent::InputPanel,
                                     true);
    return false;
  }

  if (sym == FcitxKey_Escape || sym == FcitxKey_BackSpace ||
      sym == FcitxKey_Delete) {
    inputContext.inputPanel().reset();
    inputContext.updateUserInterface(fcitx::UserInterfaceComponent::InputPanel,
                                     true);
    return true;
  }

  if (sym >= FcitxKey_1 && sym <= FcitxKey_6) {
    const int idx = sym - FcitxKey_1;
    if (idx < commonList->size()) {
      commonList->candidate(idx).select(&inputContext);
    }
    return true;
  }

  if (sym == FcitxKey_a || sym == FcitxKey_A) {
    commonList->candidate(6).select(&inputContext);
    return true;
  }

  if (sym == FcitxKey_e || sym == FcitxKey_E) {
    commonList->candidate(7).select(&inputContext);
    return true;
  }

  if (sym == FcitxKey_Up) {
    commonList->prevCandidate();
    inputContext.updateUserInterface(fcitx::UserInterfaceComponent::InputPanel,
                                     true);
    return true;
  }

  if (sym == FcitxKey_Down) {
    commonList->nextCandidate();
    inputContext.updateUserInterface(fcitx::UserInterfaceComponent::InputPanel,
                                     true);
    return true;
  }

  if (sym == FcitxKey_Return || sym == FcitxKey_KP_Enter ||
      sym == FcitxKey_space) {
    const int curIdx = commonList->cursorIndex();
    if (curIdx >= 0 && curIdx < commonList->size()) {
      commonList->candidate(curIdx).select(&inputContext);
    }
    return true;
  }

  // Chặn mọi phím khác khi menu đang mở
  return true;
}

} // namespace areca
