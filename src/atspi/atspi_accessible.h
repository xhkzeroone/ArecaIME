#pragma once

#include "dbus_raii.h"

#include <string>

namespace areca {

struct AtspiFocusInfo {
    int processId = -1;
    int role = -1;
    bool terminal = false;
    bool documentWeb = false;
    bool ignoredWindowManager = false;
    std::string program;
    std::string className;
};

class AtspiAccessible {
public:
    explicit AtspiAccessible(DBusConnection *bus) : bus_(bus) {}

    AtspiFocusInfo inspectFocus(const char *sender, const char *path) const;
    void pokeApps() const;

    static const char *roleName(int role);

private:
    DBusConnection *bus_;
};

} // namespace areca