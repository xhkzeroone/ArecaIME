#include "atspi_connection.h"
#include "focus_log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

namespace areca {

std::string AtspiConnection::environmentBusAddress() {
    const char *env = getenv("AT_SPI_BUS_ADDRESS");
    if (env && env[0]) return env;

    const char *runtimeDir = getenv("XDG_RUNTIME_DIR");
    if (runtimeDir && runtimeDir[0]) {
        std::string socketPath = std::string(runtimeDir) + "/at-spi/bus_0";
        if (access(socketPath.c_str(), F_OK) == 0) {
            return "unix:path=" + socketPath;
        }
    }

    return {};
}

std::string AtspiConnection::x11BusAddress() {
    FILE *file = popen(
        "xprop -root AT_SPI_BUS 2>/dev/null | cut -d'\"' -f2", "r");
    if (file) {
        char buffer[512] = {};
        if (fgets(buffer, sizeof(buffer), file)) {
            size_t length = strlen(buffer);
            while (length > 0 &&
                   (buffer[length - 1] == '\n' || buffer[length - 1] == '\r'))
                buffer[--length] = '\0';
            pclose(file);
            if (buffer[0]) {
                FOCUS_LOG("Bus address from X11: %s", buffer);
                return buffer;
            }
        } else {
            pclose(file);
        }
    }

    return {};
}

std::string AtspiConnection::sessionBusAddress() {
    DbusErrorGuard error;
    DbusConnectionPtr session(dbus_bus_get(DBUS_BUS_SESSION, error.get()));
    if (!session || error.isSet()) return {};

    DbusMessagePtr message(dbus_message_new_method_call(
        "org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress"));
    if (!message) return {};

    DbusMessagePtr reply(dbus_connection_send_with_reply_and_block(
        session.get(), message.get(), 200, error.get()));
    std::string address;
    if (reply && !error.isSet()) {
        const char *value = nullptr;
        DbusErrorGuard argsError;
        if (dbus_message_get_args(reply.get(), argsError.get(), DBUS_TYPE_STRING,
                                  &value, DBUS_TYPE_INVALID) != FALSE &&
            value && value[0]) {
            address = value;
        }
    }
    return address;
}

std::string AtspiConnection::findBusAddress() {
    std::string address = environmentBusAddress();
    if (!address.empty()) return address;

    address = x11BusAddress();
    if (!address.empty()) return address;

    return sessionBusAddress();
}

bool AtspiConnection::pingRegistry(DBusConnection *bus) {
    if (!bus) return false;
    DbusErrorGuard error;
    DbusMessagePtr message(dbus_message_new_method_call(
        "org.a11y.atspi.Registry", "/org/a11y/atspi/registry",
        "org.freedesktop.DBus.Peer", "Ping"));
    if (!message) return false;

    DbusMessagePtr reply(dbus_connection_send_with_reply_and_block(
        bus, message.get(), 150, error.get()));
    return reply != nullptr && !error.isSet();
}

DbusConnectionPtr AtspiConnection::connectToAddress(
    const std::string &address) {
    if (address.empty()) return {};

    DbusErrorGuard error;
    DbusConnectionPtr bus(dbus_connection_open(address.c_str(), error.get()),
                           DbusConnectionDeleter{true});
    if (bus && !error.isSet() &&
        dbus_bus_register(bus.get(), error.get()) != FALSE &&
        !error.isSet() && pingRegistry(bus.get())) {
        FOCUS_LOG("Connected to AT-SPI2 bus");
        return bus;
    }
    return {};
}

DbusConnectionPtr AtspiConnection::connectSessionBus() {
    DbusErrorGuard error;
    DbusConnectionPtr bus(dbus_bus_get(DBUS_BUS_SESSION, error.get()));
    if (bus && !error.isSet() && pingRegistry(bus.get())) {
        FOCUS_LOG("Connected to session bus fallback");
        return bus;
    }
    return {};
}

AtspiConnection::AtspiConnection() {
    connection_ = connectToAddress(findBusAddress());
    if (!connection_) connection_ = connectSessionBus();
}

} // namespace areca