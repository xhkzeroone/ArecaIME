#pragma once

#include "input_method_announcement.h"

namespace areca {

class DynamicIslandAnnouncement final : public InputMethodAnnouncement {
public:
  DynamicIslandAnnouncement(fcitx::Instance &instance,
                            InputMethodIdentity identity);

protected:
  void announce(const InputMethodIdentity &identity) override;

private:
  fcitx::Instance &instance_;
};

} // namespace areca
