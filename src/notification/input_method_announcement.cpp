#include "input_method_announcement.h"

#include <utility>
#include <fcitx/event.h>

namespace areca {

InputMethodAnnouncement::InputMethodAnnouncement(
    fcitx::Instance &instance, InputMethodIdentity identity)
    : instance_(instance), identity_(std::move(identity)) {
  watcher_ = instance_.watchEvent(
      fcitx::EventType::InputContextSwitchInputMethod,
      fcitx::EventWatcherPhase::PostInputMethod, [this](fcitx::Event &event) {
        handleSwitch(
            static_cast<fcitx::InputContextSwitchInputMethodEvent &>(event));
      });
}

void InputMethodAnnouncement::handleSwitch(
    const fcitx::InputContextSwitchInputMethodEvent &switched) {
  if (switched.oldInputMethod() == identity_.id ||
      instance_.inputMethod(switched.inputContext()) != identity_.id) {
    return;
  }
  announce(identity_);
}

} // namespace areca
