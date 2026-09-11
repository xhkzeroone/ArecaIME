#include "fcitx_dbus.h"

#include <dbus/dbus.h>

namespace areca::settings {

    bool reloadArecaAddon(std::string& errorMessage) {
        DBusError error;
        dbus_error_init(&error);
        DBusConnection* connection = dbus_bus_get(DBUS_BUS_SESSION, &error);
        if (!connection) {
            errorMessage = error.message ? error.message : "Không kết nối được D-Bus session.";
            dbus_error_free(&error);
            return false;
        }

        DBusMessage* message = dbus_message_new_method_call(
            "org.fcitx.Fcitx5", "/controller", "org.fcitx.Fcitx.Controller1", "ReloadAddonConfig"
        );
        const char* addonName = "areca";
        if (!message || !dbus_message_append_args(message, DBUS_TYPE_STRING, &addonName, DBUS_TYPE_INVALID)) {
            errorMessage = "Không tạo được yêu cầu D-Bus.";
            if (message) {
                dbus_message_unref(message);
            }
            dbus_connection_unref(connection);
            return false;
        }

        DBusMessage* reply = dbus_connection_send_with_reply_and_block(connection, message, 3000, &error);
        dbus_message_unref(message);
        dbus_connection_unref(connection);
        if (!reply) {
            errorMessage = error.message ? error.message : "Fcitx5 không phản hồi.";
            dbus_error_free(&error);
            return false;
        }
        dbus_message_unref(reply);
        return true;
    }

} // namespace areca::settings
