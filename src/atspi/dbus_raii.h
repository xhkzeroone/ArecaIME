#pragma once

#include <dbus/dbus.h>

#include <memory>

namespace areca {

class DbusErrorGuard {
public:
  DbusErrorGuard() { dbus_error_init(&error_); }
  ~DbusErrorGuard() { dbus_error_free(&error_); }

  DbusErrorGuard(const DbusErrorGuard &) = delete;
  DbusErrorGuard &operator=(const DbusErrorGuard &) = delete;

  DBusError *get() { return &error_; }
  bool isSet() const { return dbus_error_is_set(&error_) != FALSE; }

private:
  DBusError error_{};
};

struct DbusMessageDeleter {
  void operator()(DBusMessage *message) const noexcept {
    if (message)
      dbus_message_unref(message);
  }
};

struct DbusConnectionDeleter {
  bool isPrivate = false;

  void operator()(DBusConnection *connection) const noexcept {
    if (!connection)
      return;
    if (isPrivate)
      dbus_connection_close(connection);
    dbus_connection_unref(connection);
  }
};

using DbusMessagePtr = std::unique_ptr<DBusMessage, DbusMessageDeleter>;
using DbusConnectionPtr =
    std::unique_ptr<DBusConnection, DbusConnectionDeleter>;

} // namespace areca