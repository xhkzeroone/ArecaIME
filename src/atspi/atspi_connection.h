#pragma once

#include "dbus_raii.h"

#include <string>

namespace areca {

class AtspiConnection {
public:
  AtspiConnection();
  ~AtspiConnection() = default;

  AtspiConnection(const AtspiConnection &) = delete;
  AtspiConnection &operator=(const AtspiConnection &) = delete;

  DBusConnection *get() const { return connection_.get(); }

private:
  static std::string environmentBusAddress();
  static std::string x11BusAddress();
  static std::string sessionBusAddress();
  static std::string findBusAddress();
  static bool pingRegistry(DBusConnection *bus);
  static DbusConnectionPtr connectToAddress(const std::string &address);
  static DbusConnectionPtr connectSessionBus();

  DbusConnectionPtr connection_;
};

} // namespace areca