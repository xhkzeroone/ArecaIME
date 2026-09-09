#include "window_focus_tracker.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dbus/dbus.h>
#include <string_view>

namespace areca {

// AT-SPI2 role constants (from atspi-constants.h)
static constexpr int ROLE_TERMINAL = 60;
static constexpr int ROLE_DOCUMENT_WEB = 95;
static constexpr int ROLE_DOCUMENT_FRAME = 82;
static constexpr int MAX_ANCESTOR_DEPTH = 64;

static FILE *logFile() {
    static FILE *f = nullptr;
    if (!f) f = fopen("/tmp/areca_focus.log", "a");
    return f;
}

#define FOCUS_LOG(fmt, ...)                                                    \
    do {                                                                        \
        if (access("/tmp/areca_debug", F_OK) == 0) {                            \
            FILE *f = logFile();                                                \
            if (f) {                                                            \
                struct timespec ts;                                             \
                clock_gettime(CLOCK_REALTIME, &ts);                             \
                struct tm tmv;                                                  \
                localtime_r(&ts.tv_sec, &tmv);                                  \
                fprintf(f, "[focus %02d:%02d:%02d.%03ld] " fmt "\n",           \
                        tmv.tm_hour, tmv.tm_min, tmv.tm_sec,                    \
                        ts.tv_nsec / 1000000, ##__VA_ARGS__);                   \
                fflush(f);                                                      \
            }                                                                   \
        }                                                                       \
    } while (0)

#include <unistd.h>

// ---------------------------------------------------------------------------
// AT-SPI2 bus connection
// ---------------------------------------------------------------------------

static std::string getAtspiBusAddress() {
    const char *env = getenv("AT_SPI_BUS_ADDRESS");
    if (env && env[0]) return env;

    const char *runtimeDir = getenv("XDG_RUNTIME_DIR");
    if (runtimeDir && runtimeDir[0]) {
        std::string socketPath = std::string(runtimeDir) + "/at-spi/bus_0";
        if (access(socketPath.c_str(), F_OK) == 0) {
            return "unix:path=" + socketPath;
        }
    }

    FILE *fp = popen(
        "xprop -root AT_SPI_BUS 2>/dev/null | cut -d'\"' -f2",
        "r");
    if (fp) {
        char buf[512] = {};
        if (fgets(buf, sizeof(buf), fp)) {
            size_t len = strlen(buf);
            while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
                buf[--len] = '\0';
            pclose(fp);
            if (buf[0]) {
                FOCUS_LOG("Bus address from X11: %s", buf);
                return buf;
            }
        } else {
            pclose(fp);
        }
    }

    DBusError err;
    dbus_error_init(&err);
    DBusConnection *session = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (session && !dbus_error_is_set(&err)) {
        DBusMessage *msg = dbus_message_new_method_call(
            "org.a11y.Bus", "/org/a11y/bus",
            "org.a11y.Bus", "GetAddress");
        if (msg) {
            DBusMessage *reply = dbus_connection_send_with_reply_and_block(
                session, msg, 200, &err);
            dbus_message_unref(msg);
            if (reply && !dbus_error_is_set(&err)) {
                const char *s = nullptr;
                if (dbus_message_get_args(reply, &err,
                                          DBUS_TYPE_STRING, &s,
                                          DBUS_TYPE_INVALID) &&
                    s && s[0]) {
                    std::string addr = s;
                    dbus_message_unref(reply);
                    dbus_error_free(&err);
                    dbus_connection_unref(session);
                    return addr;
                }
                if (reply) dbus_message_unref(reply);
            }
        }
        dbus_error_free(&err);
        dbus_connection_unref(session);
    } else {
        dbus_error_free(&err);
    }

    return {};
}

static bool pingAtspiRegistry(DBusConnection *bus) {
    if (!bus) return false;
    DBusError err;
    dbus_error_init(&err);
    DBusMessage *msg = dbus_message_new_method_call(
        "org.a11y.atspi.Registry", "/org/a11y/atspi/registry",
        "org.freedesktop.DBus.Peer", "Ping");
    if (!msg) return false;

    DBusMessage *reply = dbus_connection_send_with_reply_and_block(
        bus, msg, 150, &err);
    dbus_message_unref(msg);
    bool ok = (reply != nullptr && !dbus_error_is_set(&err));
    if (reply) dbus_message_unref(reply);
    dbus_error_free(&err);
    return ok;
}

static DBusConnection *connectAtspiBus() {
    DBusError err;
    dbus_error_init(&err);
    std::string addr = getAtspiBusAddress();
    DBusConnection *bus = nullptr;

    if (!addr.empty()) {
        bus = dbus_connection_open(addr.c_str(), &err);
        if (bus && !dbus_error_is_set(&err)) {
            if (dbus_bus_register(bus, &err) && !dbus_error_is_set(&err)) {
                if (pingAtspiRegistry(bus)) {
                    FOCUS_LOG("Connected to AT-SPI2 bus");
                    dbus_error_free(&err);
                    return bus;
                }
            }
            dbus_connection_unref(bus);
        }
        dbus_error_free(&err);
        dbus_error_init(&err);
    }

    bus = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (bus && !dbus_error_is_set(&err)) {
        if (pingAtspiRegistry(bus)) {
            FOCUS_LOG("Connected to session bus fallback");
            dbus_error_free(&err);
            return bus;
        }
        dbus_connection_unref(bus);
    }

    dbus_error_free(&err);
    return nullptr;
}

// ---------------------------------------------------------------------------
// D-Bus helper routines
// ---------------------------------------------------------------------------

template <typename AppendFn>
static DBusMessage *callMethodWithArgs(DBusConnection *bus,
                                      const char *dest,
                                      const char *path,
                                      const char *iface,
                                      const char *method,
                                      AppendFn &&appendFn,
                                      int timeoutMs = 500) {
    if (!bus || !dest || !path || !iface || !method) return nullptr;
    DBusMessage *msg = dbus_message_new_method_call(dest, path, iface, method);
    if (!msg) return nullptr;
    appendFn(msg);
    DBusError err;
    dbus_error_init(&err);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(bus, msg, timeoutMs, &err);
    dbus_message_unref(msg);
    if (dbus_error_is_set(&err)) {
        if (reply) {
            dbus_message_unref(reply);
            reply = nullptr;
        }
        dbus_error_free(&err);
    }
    return reply;
}

static DBusMessage *callMethod(DBusConnection *bus,
                               const char *dest,
                               const char *path,
                               const char *iface,
                               const char *method,
                               int timeoutMs = 500) {
    return callMethodWithArgs(bus, dest, path, iface, method, [](DBusMessage *) {}, timeoutMs);
}

static DBusMessage *queryAccessibleProperty(DBusConnection *bus,
                                            const char *sender,
                                            const char *path,
                                            const char *propName) {
    const char *iface = "org.a11y.atspi.Accessible";
    return callMethodWithArgs(
        bus, sender, path, "org.freedesktop.DBus.Properties", "Get",
        [&](DBusMessage *msg) {
            dbus_message_append_args(msg, DBUS_TYPE_STRING, &iface,
                                     DBUS_TYPE_STRING, &propName, DBUS_TYPE_INVALID);
        });
}

// ---------------------------------------------------------------------------
// AT-SPI2 accessible queries
// ---------------------------------------------------------------------------

static int queryProcessId(DBusConnection *bus, const char *sender,
                          const char *path) {
    DBusMessage *reply = callMethod(bus, sender, path, "org.a11y.atspi.Accessible", "GetProcessId");
    if (!reply) return -1;
    dbus_int32_t p = -1;
    DBusError err;
    dbus_error_init(&err);
    int pid = -1;
    if (dbus_message_get_args(reply, &err, DBUS_TYPE_INT32, &p, DBUS_TYPE_INVALID))
        pid = static_cast<int>(p);
    dbus_message_unref(reply);
    dbus_error_free(&err);
    return pid;
}

// Resolve the pid of the focused app connection via the DBus daemon on the AT-SPI bus
static int queryConnectionPid(DBusConnection *bus, const char *sender) {
    if (!bus || !sender || !sender[0]) return -1;
    DBusMessage *reply = callMethodWithArgs(
        bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "GetConnectionUnixProcessID",
        [&](DBusMessage *msg) {
            dbus_message_append_args(msg, DBUS_TYPE_STRING, &sender, DBUS_TYPE_INVALID);
        });
    if (!reply) return -1;
    dbus_uint32_t p = 0;
    DBusError err;
    dbus_error_init(&err);
    int pid = -1;
    if (dbus_message_get_args(reply, &err, DBUS_TYPE_UINT32, &p, DBUS_TYPE_INVALID))
        pid = static_cast<int>(p);
    dbus_message_unref(reply);
    dbus_error_free(&err);
    return pid;
}

static int queryRole(DBusConnection *bus, const char *sender,
                     const char *path) {
    DBusMessage *reply = callMethod(bus, sender, path, "org.a11y.atspi.Accessible", "GetRole");
    if (!reply) return -1;
    dbus_uint32_t r = 0;
    DBusError err;
    dbus_error_init(&err);
    int role = -1;
    if (dbus_message_get_args(reply, &err, DBUS_TYPE_UINT32, &r, DBUS_TYPE_INVALID))
        role = static_cast<int>(r);
    dbus_message_unref(reply);
    dbus_error_free(&err);
    return role;
}

static bool queryParent(DBusConnection *bus, const char *sender,
                        const char *path,
                        std::string &outSender, std::string &outPath) {
    DBusMessage *reply = queryAccessibleProperty(bus, sender, path, "Parent");
    if (!reply) return false;

    DBusMessageIter iter, variant, struc;
    bool ok = false;
    if (dbus_message_iter_init(reply, &iter) &&
        dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT) {
        dbus_message_iter_recurse(&iter, &variant);
        if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_STRUCT) {
            dbus_message_iter_recurse(&variant, &struc);
            const char *parentBus = nullptr;
            const char *parentPath = nullptr;
            if (dbus_message_iter_get_arg_type(&struc) == DBUS_TYPE_STRING) {
                dbus_message_iter_get_basic(&struc, &parentBus);
                dbus_message_iter_next(&struc);
                if (dbus_message_iter_get_arg_type(&struc) == DBUS_TYPE_OBJECT_PATH)
                    dbus_message_iter_get_basic(&struc, &parentPath);
            }
            if (parentBus && parentPath && parentPath[0] == '/') {
                outSender = parentBus;
                outPath = parentPath;
                ok = true;
            }
        }
    }
    dbus_message_unref(reply);
    return ok;
}

// Human-readable names for AT-SPI2 roles observed in practice (values from
// atspi-constants.h ATSPI_ROLE_*).  Used for debug logging only.
static const char *roleName(int role) {
    switch (role) {
    case 11: return "combo_box";
    case 20: return "filler";
    case 23: return "frame";
    case 30: return "layered_pane";
    case 31: return "list";
    case 32: return "list_item";
    case 35: return "menu_item";
    case 37: return "page_tab";
    case 39: return "panel";
    case 40: return "password_text";
    case 41: return "popup_menu";
    case 43: return "button";
    case 55: return "table";
    case 56: return "table_cell";
    case 60: return "terminal";
    case 61: return "text";
    case 73: return "paragraph";
    case 79: return "entry";
    case 82: return "document_frame";
    case 85: return "section";
    case 94: return "document_text";
    case 95: return "document_web";
    case 110: return "description_list";
    default: return "?";
    }
}

static bool hasDocumentWebAncestor(DBusConnection *bus,
                                   const char *sender,
                                   const char *path) {
    std::string curSender = sender;
    std::string curPath = path;

    for (int depth = 0; depth < MAX_ANCESTOR_DEPTH; ++depth) {
        std::string parentSender, parentPath;
        if (!queryParent(bus, curSender.c_str(), curPath.c_str(),
                         parentSender, parentPath))
            break;

        if (parentPath == "/org/a11y/atspi/null" ||
            parentPath == "/org/a11y/atspi/accessible/root")
            break;

        int role = queryRole(bus, parentSender.c_str(), parentPath.c_str());
        FOCUS_LOG("  ancestor[%d]: role=%d path=%s", depth, role,
                  parentPath.c_str());
        if (role == ROLE_DOCUMENT_WEB || role == ROLE_DOCUMENT_FRAME)
            return true;

        curSender = parentSender;
        curPath = parentPath;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Chromium native accessibility activation
// ---------------------------------------------------------------------------
// Chromium (>= ~M12x, verified against 150) no longer reads
// org.a11y.Status.ScreenReaderEnabled. After a restart its accessible tree
// stays empty (no focus events for the address bar) until an AT-SPI client
// calls GetRelationSet or GetAttributes on one of its objects — Chromium
// treats those calls as "a screen reader is exploring me" and enables native
// accessibility for the rest of the browser session (AtkRefRelationSet in
// ui/accessibility/platform/ax_platform_node_auralinux.cc). Poking the app
// root of every Chromium-based browser on the bus replaces having to enable
// chrome://accessibility manually after each browser restart.

static std::string queryName(DBusConnection *bus, const char *sender,
                             const char *path) {
    DBusMessage *reply = queryAccessibleProperty(bus, sender, path, "Name");
    if (!reply) return {};
    std::string name;
    DBusMessageIter iter, variant;
    if (dbus_message_iter_init(reply, &iter) &&
        dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT) {
        dbus_message_iter_recurse(&iter, &variant);
        if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_STRING) {
            const char *s = nullptr;
            dbus_message_iter_get_basic(&variant, &s);
            if (s) name = s;
        }
    }
    dbus_message_unref(reply);
    return name;
}

static bool queryAttributesHasXterm(DBusConnection *bus, const char *sender,
                                    const char *path) {
    DBusMessage *reply = callMethod(bus, sender, path, "org.a11y.atspi.Accessible", "GetAttributes");
    if (!reply) return false;

    bool found = false;
    DBusMessageIter iter, array_iter;
    if (dbus_message_iter_init(reply, &iter) &&
        dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY) {
        dbus_message_iter_recurse(&iter, &array_iter);
        while (dbus_message_iter_get_arg_type(&array_iter) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter entry_iter;
            dbus_message_iter_recurse(&array_iter, &entry_iter);
            const char *key = nullptr;
            const char *val = nullptr;
            if (dbus_message_iter_get_arg_type(&entry_iter) == DBUS_TYPE_STRING) {
                dbus_message_iter_get_basic(&entry_iter, &key);
                dbus_message_iter_next(&entry_iter);
                if (dbus_message_iter_get_arg_type(&entry_iter) == DBUS_TYPE_STRING) {
                    dbus_message_iter_get_basic(&entry_iter, &val);
                }
            }
            if (val && val[0]) {
                std::string_view v = val;
                if (v.find("xterm") != std::string_view::npos ||
                    v.find("terminal") != std::string_view::npos) {
                    found = true;
                    break;
                }
            }
            dbus_message_iter_next(&array_iter);
        }
    }
    dbus_message_unref(reply);
    return found;
}

static bool nodeMatchesTerminalIdentity(DBusConnection *bus, const char *sender,
                                        const char *path) {
    std::string name = queryName(bus, sender, path);
    if (!name.empty()) {
        std::string lower = name;
        for (char &c : lower) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (lower.find("terminal") != std::string::npos ||
            lower.find("xterm") != std::string::npos) {
            return true;
        }
    }
    return queryAttributesHasXterm(bus, sender, path);
}

static bool isTerminalNode(DBusConnection *bus, const char *sender,
                           const char *path, int role) {
    if (role == ROLE_TERMINAL) {
        return true;
    }
    if (nodeMatchesTerminalIdentity(bus, sender, path)) {
        return true;
    }
    std::string parentSender, parentPath;
    if (queryParent(bus, sender, path, parentSender, parentPath) &&
        parentPath != "/org/a11y/atspi/null" &&
        parentPath != "/org/a11y/atspi/accessible/root") {
        int pRole = queryRole(bus, parentSender.c_str(), parentPath.c_str());
        if (pRole == ROLE_TERMINAL) {
            return true;
        }
        if (nodeMatchesTerminalIdentity(bus, parentSender.c_str(), parentPath.c_str())) {
            return true;
        }
    }
    return false;
}

static void pokeAccessibleApps(DBusConnection *bus) {
    DBusMessage *reply = callMethod(bus, "org.a11y.atspi.Registry",
                                    "/org/a11y/atspi/accessible/root",
                                    "org.a11y.atspi.Accessible", "GetChildren", 2000);
    if (!reply) return;

    DBusMessageIter iter, arr;
    if (!dbus_message_iter_init(reply, &iter) ||
        dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
        dbus_message_unref(reply);
        return;
    }
    dbus_message_iter_recurse(&iter, &arr);

    while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRUCT) {
        DBusMessageIter struc;
        dbus_message_iter_recurse(&arr, &struc);

        const char *appBus = nullptr;
        const char *appPath = nullptr;
        if (dbus_message_iter_get_arg_type(&struc) == DBUS_TYPE_STRING) {
            dbus_message_iter_get_basic(&struc, &appBus);
            dbus_message_iter_next(&struc);
            if (dbus_message_iter_get_arg_type(&struc) == DBUS_TYPE_OBJECT_PATH)
                dbus_message_iter_get_basic(&struc, &appPath);
        }

        if (appBus && appPath && appPath[0] == '/') {
            // Poke EVERY app, not just browser names: Electron apps
            // (antigravity-ide, VS Code forks) re-enable Chromium's
            // native accessibility on the same GetRelationSet/GetAttributes
            // trigger as Chrome, but their AT-SPI names don't match the
            // browser list — and after an fcitx5 restart their a11y tree
            // stays dead otherwise (no focus events for the integrated
            // terminal).  The query is read-only and cheap; non-Chromium
            // apps just return an empty relation set.
            std::string name = queryName(bus, appBus, appPath);
            DBusMessage *preply = callMethod(bus, appBus, appPath,
                                             "org.a11y.atspi.Accessible",
                                             "GetRelationSet", 500);
            if (preply) dbus_message_unref(preply);
            FOCUS_LOG("Poked '%s' (%s) to enable native a11y",
                     name.c_str(), appBus);
        }
        dbus_message_iter_next(&arr);
    }
    dbus_message_unref(reply);
}

// ---------------------------------------------------------------------------
// WindowFocusTracker
// ---------------------------------------------------------------------------

WindowFocusTracker::WindowFocusTracker() = default;

WindowFocusTracker::~WindowFocusTracker() { stop(); }

bool WindowFocusTracker::isAvailable() {
    DBusConnection *bus = connectAtspiBus();
    if (!bus) return false;
    dbus_connection_unref(bus);
    return true;
}

void WindowFocusTracker::connectionEstablished() {
    valid_.store(true, std::memory_order_relaxed);
}

void WindowFocusTracker::connectionLost() {
    valid_.store(false, std::memory_order_relaxed);
    clearFocus();
}

void WindowFocusTracker::clearFocus() {
    focusBus_.clear();
    focusPath_.clear();
    browserUIFocused_.store(false);
    focusInTerminal_.store(false);
    {
        std::lock_guard<std::mutex> lock(focusProgramMutex_);
        focusProgram_.clear();
    }
}

void WindowFocusTracker::loseFocus(const char *sender, const char *path) {
    // A delayed blur from the previous node must not clear the new focus.
    if (sender && focusBus_ == sender && (!path || focusPath_ == path))
        clearFocus();
}

bool WindowFocusTracker::start() {
    if (running_.load()) return valid_.load();
    if (!isAvailable()) {
        valid_.store(false);
        return false;
    }
    stopRequested_.store(false);
    valid_.store(true);
    thread_ = std::thread(&WindowFocusTracker::threadFunc, this);
    return true;
}

void WindowFocusTracker::stop() {
    stopRequested_.store(true);
    valid_.store(false);
    if (thread_.joinable())
        thread_.join();
}

void WindowFocusTracker::threadFunc() {
    running_.store(true);

    while (!stopRequested_.load()) {
        clearFocus();
        DBusConnection *bus = connectAtspiBus();
        if (!bus) {
            connectionLost();
            for (int i = 0; i < 20 && !stopRequested_.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            continue;
        }

    // Register with AT-SPI2 registry for focus events
    DBusError err;
    dbus_error_init(&err);

    auto registerEvent = [&](const char *eventName) {
        DBusMessage *reply = callMethodWithArgs(
            bus, "org.a11y.atspi.Registry", "/org/a11y/atspi/registry",
            "org.a11y.atspi.Registry", "RegisterEvent",
            [&](DBusMessage *msg) {
                dbus_message_append_args(msg, DBUS_TYPE_STRING, &eventName,
                                         DBUS_TYPE_INVALID);
            }, 2000);
        const bool ok = reply != nullptr;
        if (reply) dbus_message_unref(reply);
        return ok;
    };

    if (!registerEvent("object:state-changed:focused") ||
        !registerEvent("focus:")) {
        connectionLost();
        dbus_connection_unref(bus);
        for (int i = 0; i < 20 && !stopRequested_.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        continue;
    }

    dbus_bus_add_match(bus,
                       "type='signal',"
                       "interface='org.a11y.atspi.Event.Object',"
                       "member='StateChanged'",
                       &err);
    dbus_error_free(&err);
    dbus_error_init(&err);
    dbus_bus_add_match(bus,
                       "type='signal',"
                       "interface='org.a11y.atspi.Event.Focus'",
                       &err);
    dbus_error_free(&err);
    dbus_error_init(&err);
    // New connections joining the a11y bus (e.g. a browser starting up)
    dbus_bus_add_match(bus,
                       "type='signal',sender='org.freedesktop.DBus',"
                       "interface='org.freedesktop.DBus',"
                       "member='NameOwnerChanged'",
                       &err);
    dbus_error_free(&err);
    connectionEstablished();
    FOCUS_LOG("WindowFocusTracker started");

    // Poke browsers already on the bus, then re-poke whenever a new app
    // connects (short + late retry: the app root only becomes queryable once
    // the browser's ATK bridge has registered with the registry), plus a
    // periodic sweep as a fallback.
    pokeAccessibleApps(bus);

    using Clock = std::chrono::steady_clock;
    const auto kNever = Clock::time_point::max();
    Clock::time_point pokeAt = kNever;
    Clock::time_point latePokeAt = kNever;
    Clock::time_point periodicPokeAt =
        Clock::now() + std::chrono::seconds(15);

    // Poll loop
    while (!stopRequested_.load()) {
        if (!dbus_connection_read_write(bus, 30)) {
            connectionLost();
            break;
        }

        DBusMessage *msg;
        while ((msg = dbus_connection_pop_message(bus)) != nullptr) {
            const char *iface = dbus_message_get_interface(msg);
            const char *member = dbus_message_get_member(msg);

            bool isFocusEvent = false;

            if (iface && member &&
                strcmp(iface, "org.a11y.atspi.Event.Object") == 0 &&
                strcmp(member, "StateChanged") == 0) {
                DBusMessageIter iter;
                if (dbus_message_iter_init(msg, &iter) &&
                    dbus_message_iter_get_arg_type(&iter) ==
                        DBUS_TYPE_STRING) {
                    const char *stateName = nullptr;
                    dbus_message_iter_get_basic(&iter, &stateName);
                    if (stateName && strcmp(stateName, "focused") == 0) {
                        dbus_message_iter_next(&iter);
                        if (dbus_message_iter_get_arg_type(&iter) ==
                            DBUS_TYPE_INT32) {
                            dbus_int32_t d1 = 0;
                            dbus_message_iter_get_basic(&iter, &d1);
                            if (d1 == 1)
                                isFocusEvent = true;
                            else if (d1 == 0)
                                loseFocus(dbus_message_get_sender(msg),
                                          dbus_message_get_path(msg));
                        }
                    }
                }
            }

            if (iface && member &&
                strcmp(iface, "org.a11y.atspi.Event.Focus") == 0)
                isFocusEvent = true;

            if (iface && member &&
                strcmp(iface, "org.freedesktop.DBus") == 0 &&
                strcmp(member, "NameOwnerChanged") == 0) {
                const char *busName = nullptr;
                const char *oldOwner = nullptr;
                const char *newOwner = nullptr;
                DBusError nerr;
                dbus_error_init(&nerr);
                if (dbus_message_get_args(msg, &nerr,
                                          DBUS_TYPE_STRING, &busName,
                                          DBUS_TYPE_STRING, &oldOwner,
                                          DBUS_TYPE_STRING, &newOwner,
                                          DBUS_TYPE_INVALID)) {
                    if (oldOwner && oldOwner[0] && newOwner && !newOwner[0])
                        loseFocus(busName);
                    if (newOwner && newOwner[0]) {
                        auto now = Clock::now();
                        pokeAt = now + std::chrono::milliseconds(600);
                        latePokeAt = now + std::chrono::milliseconds(3000);
                    }
                }
                dbus_error_free(&nerr);
            }

            if (isFocusEvent) {
                const char *sender = dbus_message_get_sender(msg);
                const char *path = dbus_message_get_path(msg);
                if (sender && path) {
                    clearFocus();
                    focusBus_ = sender;
                    focusPath_ = path;
                    int role = queryRole(bus, sender, path);
                    bool isTerm = isTerminalNode(bus, sender, path, role);
                    focusInTerminal_.store(isTerm,
                                           std::memory_order_relaxed);
                    bool hasDocWeb = !isTerm && hasDocumentWebAncestor(
                        bus, sender, path);
                    // Resolve the PID through the a11y registry first. This
                    // avoids a potentially blocking call to browser UI nodes.
                    int procId = queryConnectionPid(bus, sender);
                    if (procId <= 0 && hasDocWeb) {
                        procId = queryProcessId(bus, sender, path);
                    }
                    if (procId > 0) {
                        char commBuf[64] = {};
                        char commPath[64];
                        snprintf(commPath, sizeof(commPath), "/proc/%d/comm", procId);
                        FILE *cf = fopen(commPath, "r");
                        if (cf) {
                            if (fgets(commBuf, sizeof(commBuf), cf)) {
                                size_t len = strlen(commBuf);
                                while (len > 0 && (commBuf[len - 1] == '\n' || commBuf[len - 1] == '\r'))
                                    commBuf[--len] = '\0';
                            }
                            fclose(cf);
                        }
                        if (commBuf[0] != '\0') {
                            std::string_view commView(commBuf);
                            if (commView == "plasmashell" || commView == "kwin_wayland" ||
                                commView == "gnome-shell" || commView == "mutter") {
                                clearFocus();
                                dbus_message_unref(msg);
                                continue;
                            }
                            {
                                std::lock_guard<std::mutex> lock(focusProgramMutex_);
                                focusProgram_ = commBuf;
                            }
                        }
                    }
                    bool isUI = !hasDocWeb;
                    browserUIFocused_.store(isUI,
                                           std::memory_order_relaxed);
                    FOCUS_LOG("Focus: terminal=%d webDoc=%d role=%d(%s) "
                              "pid=%d path=%s",
                              isTerm, hasDocWeb, role, roleName(role), procId,
                              path);
                }
            }

            dbus_message_unref(msg);
        }

        auto now = Clock::now();
        if (now >= pokeAt || now >= latePokeAt || now >= periodicPokeAt) {
            if (now >= pokeAt) pokeAt = kNever;
            if (now >= latePokeAt) latePokeAt = kNever;
            if (now >= periodicPokeAt)
                periodicPokeAt = now + std::chrono::seconds(15);
            pokeAccessibleApps(bus);
        }
    }

        dbus_connection_unref(bus);
        connectionLost();

        if (!stopRequested_.load()) {
            for (int i = 0; i < 10 && !stopRequested_.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    connectionLost();
    running_.store(false);
    FOCUS_LOG("WindowFocusTracker stopped");
}

} // namespace areca
