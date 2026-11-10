#include "atspi_accessible.h"

#include "program_compatibility.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace areca {
namespace {

constexpr int ROLE_TERMINAL = 60;
constexpr int ROLE_DOCUMENT_WEB = 95;
constexpr int ROLE_DOCUMENT_FRAME = 82;
constexpr int MAX_ANCESTOR_DEPTH = 64;

template <typename AppendFn>
DbusMessagePtr callMethodWithArgs(DBusConnection *bus, const char *destination,
                                  const char *path, const char *interface,
                                  const char *method, AppendFn &&appendFn,
                                  int timeoutMs = 500) {
  if (!bus || !destination || !path || !interface || !method)
    return nullptr;
  DbusMessagePtr message(
      dbus_message_new_method_call(destination, path, interface, method));
  if (!message)
    return nullptr;
  appendFn(message.get());

  DbusErrorGuard error;
  DbusMessagePtr reply(dbus_connection_send_with_reply_and_block(
      bus, message.get(), timeoutMs, error.get()));
  if (error.isSet())
    return {};
  return reply;
}

DbusMessagePtr callMethod(DBusConnection *bus, const char *destination,
                          const char *path, const char *interface,
                          const char *method, int timeoutMs = 500) {
  return callMethodWithArgs(
      bus, destination, path, interface, method, [](DBusMessage *) {},
      timeoutMs);
}

DbusMessagePtr queryAccessibleProperty(DBusConnection *bus, const char *sender,
                                       const char *path, const char *property) {
  const char *interface = "org.a11y.atspi.Accessible";
  return callMethodWithArgs(
      bus, sender, path, "org.freedesktop.DBus.Properties", "Get",
      [&](DBusMessage *message) {
        dbus_message_append_args(message, DBUS_TYPE_STRING, &interface,
                                 DBUS_TYPE_STRING, &property,
                                 DBUS_TYPE_INVALID);
      });
}

int queryProcessId(DBusConnection *bus, const char *sender, const char *path) {
  DbusMessagePtr reply = callMethod(
      bus, sender, path, "org.a11y.atspi.Accessible", "GetProcessId");
  if (!reply)
    return -1;

  dbus_int32_t processId = -1;
  DbusErrorGuard error;
  int result = -1;
  if (dbus_message_get_args(reply.get(), error.get(), DBUS_TYPE_INT32,
                            &processId, DBUS_TYPE_INVALID)) {
    result = static_cast<int>(processId);
  }
  return result;
}

int queryConnectionPid(DBusConnection *bus, const char *sender) {
  if (!bus || !sender || !sender[0])
    return -1;
  DbusMessagePtr reply =
      callMethodWithArgs(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                         "org.freedesktop.DBus", "GetConnectionUnixProcessID",
                         [&](DBusMessage *message) {
                           dbus_message_append_args(message, DBUS_TYPE_STRING,
                                                    &sender, DBUS_TYPE_INVALID);
                         });
  if (!reply)
    return -1;

  dbus_uint32_t processId = 0;
  DbusErrorGuard error;
  int result = -1;
  if (dbus_message_get_args(reply.get(), error.get(), DBUS_TYPE_UINT32,
                            &processId, DBUS_TYPE_INVALID)) {
    result = static_cast<int>(processId);
  }
  return result;
}

int queryRole(DBusConnection *bus, const char *sender, const char *path) {
  DbusMessagePtr reply =
      callMethod(bus, sender, path, "org.a11y.atspi.Accessible", "GetRole");
  if (!reply)
    return -1;

  dbus_uint32_t role = 0;
  DbusErrorGuard error;
  int result = -1;
  if (dbus_message_get_args(reply.get(), error.get(), DBUS_TYPE_UINT32, &role,
                            DBUS_TYPE_INVALID)) {
    result = static_cast<int>(role);
  }
  return result;
}

bool queryParent(DBusConnection *bus, const char *sender, const char *path,
                 std::string &parentSender, std::string &parentPath) {
  DbusMessagePtr reply = queryAccessibleProperty(bus, sender, path, "Parent");
  if (!reply)
    return false;

  DBusMessageIter iter, variant, structure;
  bool found = false;
  if (dbus_message_iter_init(reply.get(), &iter) &&
      dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT) {
    dbus_message_iter_recurse(&iter, &variant);
    if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_STRUCT) {
      dbus_message_iter_recurse(&variant, &structure);
      const char *busName = nullptr;
      const char *objectPath = nullptr;
      if (dbus_message_iter_get_arg_type(&structure) == DBUS_TYPE_STRING) {
        dbus_message_iter_get_basic(&structure, &busName);
        dbus_message_iter_next(&structure);
        if (dbus_message_iter_get_arg_type(&structure) ==
            DBUS_TYPE_OBJECT_PATH) {
          dbus_message_iter_get_basic(&structure, &objectPath);
        }
      }
      if (busName && objectPath && objectPath[0] == '/') {
        parentSender = busName;
        parentPath = objectPath;
        found = true;
      }
    }
  }
  return found;
}

std::string queryFocusClass(DBusConnection *bus, const char *sender,
                            const char *path) {
  DbusMessagePtr reply = callMethod(
      bus, sender, path, "org.a11y.atspi.Accessible", "GetAttributes");
  std::string className;
  const bool valid = reply && dbus_message_has_signature(reply.get(), "a{ss}");
  if (valid) {
    DBusMessageIter iter, entries;
    dbus_message_iter_init(reply.get(), &iter);
    dbus_message_iter_recurse(&iter, &entries);
    while (dbus_message_iter_get_arg_type(&entries) == DBUS_TYPE_DICT_ENTRY) {
      DBusMessageIter entry;
      dbus_message_iter_recurse(&entries, &entry);
      const char *key = nullptr;
      const char *value = nullptr;
      dbus_message_iter_get_basic(&entry, &key);
      dbus_message_iter_next(&entry);
      dbus_message_iter_get_basic(&entry, &value);
      if (strcmp(key, "class") == 0) {
        className = value;
        break;
      }
      dbus_message_iter_next(&entries);
    }
  }
  return className;
}

std::string programForPid(int processId) {
  if (processId <= 0)
    return {};
  char path[64];
  char name[64] = {};
  snprintf(path, sizeof(path), "/proc/%d/comm", processId);
  FILE *file = fopen(path, "r");
  if (!file)
    return {};
  const bool read = fgets(name, sizeof(name), file) != nullptr;
  fclose(file);
  if (!read)
    return {};
  name[strcspn(name, "\r\n")] = '\0';
  return name;
}

bool hasDocumentWebAncestor(DBusConnection *bus, const char *sender,
                            const char *path) {
  std::string currentSender = sender;
  std::string currentPath = path;

  for (int depth = 0; depth < MAX_ANCESTOR_DEPTH; ++depth) {
    std::string parentSender;
    std::string parentPath;
    if (!queryParent(bus, currentSender.c_str(), currentPath.c_str(),
                     parentSender, parentPath)) {
      break;
    }
    if (parentPath == "/org/a11y/atspi/null" ||
        parentPath == "/org/a11y/atspi/accessible/root") {
      break;
    }

    const int role = queryRole(bus, parentSender.c_str(), parentPath.c_str());
    if (role == ROLE_DOCUMENT_WEB || role == ROLE_DOCUMENT_FRAME)
      return true;

    currentSender = parentSender;
    currentPath = parentPath;
  }
  return false;
}

std::string queryName(DBusConnection *bus, const char *sender,
                      const char *path) {
  DbusMessagePtr reply = queryAccessibleProperty(bus, sender, path, "Name");
  if (!reply)
    return {};

  std::string name;
  DBusMessageIter iter, variant;
  if (dbus_message_iter_init(reply.get(), &iter) &&
      dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT) {
    dbus_message_iter_recurse(&iter, &variant);
    if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_STRING) {
      const char *value = nullptr;
      dbus_message_iter_get_basic(&variant, &value);
      if (value)
        name = value;
    }
  }
  return name;
}

bool queryAttributesHasXterm(DBusConnection *bus, const char *sender,
                             const char *path) {
  DbusMessagePtr reply = callMethod(
      bus, sender, path, "org.a11y.atspi.Accessible", "GetAttributes");
  if (!reply)
    return false;

  bool found = false;
  DBusMessageIter iter, attributes;
  if (dbus_message_iter_init(reply.get(), &iter) &&
      dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY) {
    dbus_message_iter_recurse(&iter, &attributes);
    while (dbus_message_iter_get_arg_type(&attributes) ==
           DBUS_TYPE_DICT_ENTRY) {
      DBusMessageIter entry;
      dbus_message_iter_recurse(&attributes, &entry);
      const char *key = nullptr;
      const char *value = nullptr;
      if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING) {
        dbus_message_iter_get_basic(&entry, &key);
        dbus_message_iter_next(&entry);
        if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING) {
          dbus_message_iter_get_basic(&entry, &value);
        }
      }
      if (value && value[0]) {
        const std::string_view text = value;
        if (text.find("xterm") != std::string_view::npos ||
            text.find("terminal") != std::string_view::npos) {
          found = true;
          break;
        }
      }
      dbus_message_iter_next(&attributes);
    }
  }
  return found;
}

bool nodeMatchesTerminalIdentity(DBusConnection *bus, const char *sender,
                                 const char *path) {
  std::string name = queryName(bus, sender, path);
  for (char &character : name) {
    character =
        static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  if (name.find("terminal") != std::string::npos ||
      name.find("xterm") != std::string::npos) {
    return true;
  }
  return queryAttributesHasXterm(bus, sender, path);
}

bool isTerminalNode(DBusConnection *bus, const char *sender, const char *path,
                    int role) {
  if (role == ROLE_TERMINAL)
    return true;
  if (nodeMatchesTerminalIdentity(bus, sender, path))
    return true;

  std::string parentSender;
  std::string parentPath;
  if (!queryParent(bus, sender, path, parentSender, parentPath) ||
      parentPath == "/org/a11y/atspi/null" ||
      parentPath == "/org/a11y/atspi/accessible/root") {
    return false;
  }
  if (queryRole(bus, parentSender.c_str(), parentPath.c_str()) == ROLE_TERMINAL)
    return true;
  return nodeMatchesTerminalIdentity(bus, parentSender.c_str(),
                                     parentPath.c_str());
}

bool isWindowManagerProcess(const std::string &program) {
  return program == "plasmashell" || program == "kwin_wayland" ||
         program == "gnome-shell" || program == "mutter";
}

} // namespace

AtspiFocusInfo AtspiAccessible::inspectFocus(const char *sender,
                                             const char *path) const {
  AtspiFocusInfo info;
  info.processId = queryConnectionPid(bus_, sender);
  info.program = programForPid(info.processId);
  if (isWindowManagerProcess(info.program)) {
    info.ignoredWindowManager = true;
    return info;
  }

  if (isChromiumBrowser(info.program)) {
    info.className = queryFocusClass(bus_, sender, path);
  }
  if (info.className.find("Omnibox") != std::string::npos)
    return info;

  info.role = queryRole(bus_, sender, path);
  info.terminal = isTerminalNode(bus_, sender, path, info.role);
  info.documentWeb =
      !info.terminal && hasDocumentWebAncestor(bus_, sender, path);
  if (info.processId <= 0 && info.documentWeb) {
    info.processId = queryProcessId(bus_, sender, path);
    info.program = programForPid(info.processId);
    if (isChromiumBrowser(info.program)) {
      info.className = queryFocusClass(bus_, sender, path);
    }
  }
  return info;
}

void AtspiAccessible::pokeApps() const {
  DbusMessagePtr reply = callMethod(
      bus_, "org.a11y.atspi.Registry", "/org/a11y/atspi/accessible/root",
      "org.a11y.atspi.Accessible", "GetChildren", 2000);
  if (!reply)
    return;

  DBusMessageIter iter;
  if (!dbus_message_iter_init(reply.get(), &iter) ||
      dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
    return;
  }

  DBusMessageIter applications;
  dbus_message_iter_recurse(&iter, &applications);
  while (dbus_message_iter_get_arg_type(&applications) == DBUS_TYPE_STRUCT) {
    DBusMessageIter application;
    dbus_message_iter_recurse(&applications, &application);
    const char *appBus = nullptr;
    const char *appPath = nullptr;
    if (dbus_message_iter_get_arg_type(&application) == DBUS_TYPE_STRING) {
      dbus_message_iter_get_basic(&application, &appBus);
      dbus_message_iter_next(&application);
      if (dbus_message_iter_get_arg_type(&application) ==
          DBUS_TYPE_OBJECT_PATH) {
        dbus_message_iter_get_basic(&application, &appPath);
      }
    }

    if (appBus && appPath && appPath[0] == '/') {
      DbusMessagePtr poke =
          callMethod(bus_, appBus, appPath, "org.a11y.atspi.Accessible",
                     "GetRelationSet", 500);
    }
    dbus_message_iter_next(&applications);
  }
}

} // namespace areca