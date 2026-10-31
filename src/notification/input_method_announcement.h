#pragma once

#include <memory>
#include <string>
#include <fcitx/instance.h>

namespace areca {

struct InputMethodIdentity {
  std::string id;
  std::string name;
  std::string icon;
};

// Watches user switches independently of the announcement destination.
class InputMethodAnnouncement {
public:
  InputMethodAnnouncement(fcitx::Instance &instance, InputMethodIdentity identity);
  virtual ~InputMethodAnnouncement() = default;

protected:
  virtual void announce(const InputMethodIdentity &identity) = 0;

private:
  void handleSwitch(const fcitx::InputContextSwitchInputMethodEvent &event);

  fcitx::Instance &instance_;
  InputMethodIdentity identity_;
  std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>> watcher_;
};

} // namespace areca
