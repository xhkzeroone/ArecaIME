#include "areca_engine.h"

#include <fcitx-utils/log.h>
#include <fcitx/inputcontext.h>
#include <fcitx/statusarea.h>
#include <fcitx/userinterfacemanager.h>

namespace areca {

void ArecaEngine::initActions() {
  auto &uiManager = instance_->userInterfaceManager();

  inputMethodAction_ = std::make_unique<fcitx::SimpleAction>();
  inputMethodAction_->setIcon("document-edit");
  inputMethodAction_->setShortText(_("Kiểu gõ"));
  uiManager.registerAction("areca-input-method", inputMethodAction_.get());

  inputMethodMenu_ = std::make_unique<fcitx::Menu>();
  inputMethodAction_->setMenu(inputMethodMenu_.get());

  inputMethodNames_ = BambooEngineAdapter::inputMethodNames();
  inputMethodSubActions_.reserve(inputMethodNames_.size());
  for (size_t i = 0; i < inputMethodNames_.size(); ++i) {
    const auto &imName = inputMethodNames_[i];
    auto subAction = std::make_unique<fcitx::SimpleAction>();
    subAction->setShortText(imName);
    subAction->setCheckable(true);
    uiManager.registerAction("areca-im-" + std::to_string(i), subAction.get());
    actionConnections_.emplace_back(
        subAction->connect<fcitx::SimpleAction::Activated>(
            [this, imName](fcitx::InputContext *ic) {
              config_.bambooInputMethod.setValue(imName);
              applyConfig();
              save();
              updateUI(ic);
              if (ic) {
                instance_->showInputMethodInformation(ic);
              }
            }));
    inputMethodMenu_->addAction(subAction.get());
    inputMethodSubActions_.push_back(std::move(subAction));
  }

  charsetAction_ = std::make_unique<fcitx::SimpleAction>();
  charsetAction_->setIcon("character-set");
  charsetAction_->setShortText(_("Bảng mã"));
  uiManager.registerAction("areca-charset", charsetAction_.get());

  charsetMenu_ = std::make_unique<fcitx::Menu>();
  charsetAction_->setMenu(charsetMenu_.get());

  charsetNames_ = BambooEngineAdapter::charsetNames();
  charsetSubActions_.reserve(charsetNames_.size());
  for (size_t i = 0; i < charsetNames_.size(); ++i) {
    const auto &csName = charsetNames_[i];
    auto subAction = std::make_unique<fcitx::SimpleAction>();
    subAction->setShortText(csName);
    subAction->setCheckable(true);
    uiManager.registerAction("areca-cs-" + std::to_string(i), subAction.get());
    actionConnections_.emplace_back(
        subAction->connect<fcitx::SimpleAction::Activated>(
            [this, csName](fcitx::InputContext *ic) {
              config_.outputCharset.setValue(csName);
              applyConfig();
              save();
              updateUI(ic);
              if (ic) {
                instance_->showInputMethodInformation(ic);
              }
            }));
    charsetMenu_->addAction(subAction.get());
    charsetSubActions_.push_back(std::move(subAction));
  }

  presentationModeAction_ = std::make_unique<fcitx::SimpleAction>();
  presentationModeAction_->setIcon("preferences-system");
  presentationModeAction_->setShortText(_("Chế độ hiển thị"));
  uiManager.registerAction("areca-presentation-mode",
                           presentationModeAction_.get());

  presentationModeMenu_ = std::make_unique<fcitx::Menu>();
  presentationModeAction_->setMenu(presentationModeMenu_.get());

  struct PresentationModeOption {
    PresentationMode mode;
    const char *label;
  };
  const PresentationModeOption presModes[] = {
      {PresentationMode::Rewrite, N_("Rewrite trực tiếp")},
      {PresentationMode::Preedit, N_("Preedit")},
      {PresentationMode::Redirect, N_("Redirect")},
  };
  presentationModeSubActions_.reserve(std::size(presModes));
  for (size_t i = 0; i < std::size(presModes); ++i) {
    const auto mode = presModes[i].mode;
    auto subAction = std::make_unique<fcitx::SimpleAction>();
    subAction->setShortText(_(presModes[i].label));
    subAction->setCheckable(true);
    uiManager.registerAction("areca-pres-mode-" + std::to_string(i),
                             subAction.get());
    actionConnections_.emplace_back(
        subAction->connect<fcitx::SimpleAction::Activated>(
            [this, mode](fcitx::InputContext *ic) {
              if (config_.presentationMode.value() != mode) {
                if (ic) {
                  rewriteHandler_.resetContext(*ic);
                  preeditHandler_.resetContext(*ic);
                }
                config_.presentationMode.setValue(mode);
                applyConfig();
                if (ic) {
                  handlerFor(*ic).activate(*ic);
                }
                save();
                updateUI(ic);
                if (ic) {
                  instance_->showInputMethodInformation(ic);
                }
              }
            }));
    presentationModeMenu_->addAction(subAction.get());
    presentationModeSubActions_.push_back(std::move(subAction));
  }

  spellcheckAction_ = std::make_unique<fcitx::SimpleAction>();
  spellcheckAction_->setIcon("tools-check-spelling");
  spellcheckAction_->setShortText(_("Kiểm tra chính tả"));
  uiManager.registerAction("areca-spellcheck-mode", spellcheckAction_.get());

  spellcheckMenu_ = std::make_unique<fcitx::Menu>();
  spellcheckAction_->setMenu(spellcheckMenu_.get());

  struct SpellcheckModeOption {
    SpellcheckMode mode;
    const char *label;
  };
  const SpellcheckModeOption spellModes[] = {
      {SpellcheckMode::Off, N_("Không kiểm tra chính tả")},
      {SpellcheckMode::Basic, N_("Khôi phục từ sau khi gõ xong")},
      {SpellcheckMode::Realtime, N_("Khôi phục từ ngay trong lúc gõ")},
  };
  spellcheckSubActions_.reserve(std::size(spellModes));
  for (size_t i = 0; i < std::size(spellModes); ++i) {
    const auto mode = spellModes[i].mode;
    auto subAction = std::make_unique<fcitx::SimpleAction>();
    subAction->setShortText(_(spellModes[i].label));
    subAction->setCheckable(true);
    uiManager.registerAction("areca-spellcheck-" + std::to_string(i),
                             subAction.get());
    actionConnections_.emplace_back(
        subAction->connect<fcitx::SimpleAction::Activated>(
            [this, mode](fcitx::InputContext *ic) {
              config_.spellcheckMode.setValue(mode);
              applyConfig();
              save();
              updateUI(ic);
            }));
    spellcheckMenu_->addAction(subAction.get());
    spellcheckSubActions_.push_back(std::move(subAction));
  }

  modernStyleAction_ = std::make_unique<fcitx::SimpleAction>();
  modernStyleAction_->setShortText(_("Dấu kiểu mới"));
  modernStyleAction_->setCheckable(true);
  uiManager.registerAction("areca-modern-style", modernStyleAction_.get());
  actionConnections_.emplace_back(
      modernStyleAction_->connect<fcitx::SimpleAction::Activated>(
          [this](fcitx::InputContext *ic) {
            config_.modernStyle.setValue(!config_.modernStyle.value());
            applyConfig();
            save();
            updateUI(ic);
          }));

  macroAction_ = std::make_unique<fcitx::SimpleAction>();
  macroAction_->setIcon("edit-find");
  macroAction_->setShortText(_("Bật gõ tắt"));
  macroAction_->setCheckable(true);
  uiManager.registerAction("areca-enable-macro", macroAction_.get());
  actionConnections_.emplace_back(
      macroAction_->connect<fcitx::SimpleAction::Activated>(
          [this](fcitx::InputContext *ic) {
            config_.enableMacro.setValue(!config_.enableMacro.value());
            applyConfig();
            save();
            updateUI(ic);
          }));

  autoCapitalizeAction_ = std::make_unique<fcitx::SimpleAction>();
  autoCapitalizeAction_->setShortText(_("Tự viết hoa sau dấu kết câu"));
  autoCapitalizeAction_->setCheckable(true);
  uiManager.registerAction("areca-auto-capitalize",
                           autoCapitalizeAction_.get());
  actionConnections_.emplace_back(
      autoCapitalizeAction_->connect<fcitx::SimpleAction::Activated>(
          [this](fcitx::InputContext *ic) {
            config_.autoCapitalizeAfterPunctuation.setValue(
                !config_.autoCapitalizeAfterPunctuation.value());
            applyConfig();
            save();
            updateUI(ic);
          }));

  settingsAction_ = std::make_unique<fcitx::SimpleAction>();
  settingsAction_->setShortText(_("Cài đặt Areca..."));
  settingsAction_->setIcon("configure");
  actionConnections_.emplace_back(
      settingsAction_->connect<fcitx::SimpleAction::Activated>(
          [](fcitx::InputContext *) {
            fcitx::startProcess({ARECA_SETTINGS_PATH});
          }));
  uiManager.registerAction("areca-settings", settingsAction_.get());

  updateUI(nullptr);
}

void ArecaEngine::updateUI(fcitx::InputContext *ic) {
  updateInputMethodAction(ic);
  updateCharsetAction(ic);
  updatePresentationModeAction(ic);
  updateSpellcheckAction(ic);
  updateModernStyleAction(ic);
  updateMacroAction(ic);
  updateAutoCapitalizeAction(ic);
}

void ArecaEngine::updateInputMethodAction(fcitx::InputContext *ic) {
  const auto current = config_.bambooInputMethod.value();
  for (size_t i = 0; i < inputMethodSubActions_.size(); ++i) {
    inputMethodSubActions_[i]->setChecked(inputMethodNames_[i] == current);
    inputMethodSubActions_[i]->update(ic);
  }
  inputMethodAction_->setLongText(current);
  inputMethodAction_->update(ic);
}

void ArecaEngine::updateCharsetAction(fcitx::InputContext *ic) {
  const auto current = config_.outputCharset.value();
  for (size_t i = 0; i < charsetSubActions_.size(); ++i) {
    charsetSubActions_[i]->setChecked(charsetNames_[i] == current);
    charsetSubActions_[i]->update(ic);
  }
  charsetAction_->setLongText(current);
  charsetAction_->update(ic);
}

void ArecaEngine::updatePresentationModeAction(fcitx::InputContext *ic) {
  const auto current = config_.presentationMode.value();
  if (presentationModeSubActions_.size() == 3) {
    presentationModeSubActions_[0]->setChecked(current ==
                                               PresentationMode::Rewrite);
    presentationModeSubActions_[0]->update(ic);
    presentationModeSubActions_[1]->setChecked(current ==
                                               PresentationMode::Preedit);
    presentationModeSubActions_[1]->update(ic);
    presentationModeSubActions_[2]->setChecked(current ==
                                               PresentationMode::Redirect);
    presentationModeSubActions_[2]->update(ic);
  }
  presentationModeAction_->setLongText(presentationModeName(current));
  presentationModeAction_->update(ic);
}

void ArecaEngine::updateSpellcheckAction(fcitx::InputContext *ic) {
  const auto current = config_.spellcheckMode.value();
  if (spellcheckSubActions_.size() == 3) {
    spellcheckSubActions_[0]->setChecked(current == SpellcheckMode::Off);
    spellcheckSubActions_[0]->update(ic);
    spellcheckSubActions_[1]->setChecked(current == SpellcheckMode::Basic);
    spellcheckSubActions_[1]->update(ic);
    spellcheckSubActions_[2]->setChecked(current == SpellcheckMode::Realtime);
    spellcheckSubActions_[2]->update(ic);
  }
  switch (current) {
  case SpellcheckMode::Off:
    spellcheckAction_->setLongText(_("Không kiểm tra chính tả"));
    break;
  case SpellcheckMode::Basic:
    spellcheckAction_->setLongText(_("Khôi phục từ sau khi gõ xong"));
    break;
  case SpellcheckMode::Realtime:
    spellcheckAction_->setLongText(_("Khôi phục từ ngay trong lúc gõ"));
    break;
  }
  spellcheckAction_->update(ic);
}

void ArecaEngine::updateModernStyleAction(fcitx::InputContext *ic) {
  modernStyleAction_->setChecked(config_.modernStyle.value());
  modernStyleAction_->update(ic);
}

void ArecaEngine::updateMacroAction(fcitx::InputContext *ic) {
  macroAction_->setChecked(config_.enableMacro.value());
  macroAction_->update(ic);
}

void ArecaEngine::updateAutoCapitalizeAction(fcitx::InputContext *ic) {
  autoCapitalizeAction_->setChecked(
      config_.autoCapitalizeAfterPunctuation.value());
  autoCapitalizeAction_->update(ic);
}

void ArecaEngine::syncInputMethodEntryIcon(
    const fcitx::InputMethodEntry &entry) {
  const std::string icon = overrideIcon(entry);
  if (icon == inputMethodEntryIcon_) {
    return;
  }
  const_cast<fcitx::InputMethodEntry &>(entry).setIcon(icon);
  inputMethodEntryIcon_ = icon;
}

} // namespace areca
