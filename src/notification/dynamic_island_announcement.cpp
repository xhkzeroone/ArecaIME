#include "dynamic_island_announcement.h"

#include <utility>
#include <fcitx-utils/dbus/bus.h>
#include <fcitx-utils/dbus/message.h>
#include <fcitx/addonmanager.h>

FCITX_ADDON_DECLARE_FUNCTION(DBusModule, bus, fcitx::dbus::Bus *());

namespace areca {

DynamicIslandAnnouncement::DynamicIslandAnnouncement(
    fcitx::Instance &instance, InputMethodIdentity identity)
    : InputMethodAnnouncement(instance, std::move(identity)), instance_(instance) {}

void DynamicIslandAnnouncement::announce(const InputMethodIdentity &identity) {
  // Use the existing event-loop connection; never wait for Island.
  auto *addon = instance_.addonManager().addon("dbus", true);
  auto *bus = addon ? addon->call<fcitx::IDBusModule::bus>() : nullptr;
  if (!bus) return;
  auto signal = bus->createSignal(
      "/com/github/dynamic_island/InputMethod",
      "com.github.dynamic_island.InputMethod", "Activated");
  signal << identity.id << identity.name << identity.icon;
  signal.send();
}

} // namespace areca
