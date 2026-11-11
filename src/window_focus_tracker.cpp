#include "window_focus_tracker.h"

#include "atspi/atspi_accessible.h"
#include "atspi/atspi_connection.h"

#include <chrono>
#include <cstring>
#include <dbus/dbus.h>
#include <fcitx-utils/log.h>

namespace areca {

struct WindowFocusTracker::PokeSchedule {
  using Clock = std::chrono::steady_clock;

  Clock::time_point pokeAt = Clock::time_point::max();
  Clock::time_point latePokeAt = Clock::time_point::max();

  void applicationConnected() {
    const auto now = Clock::now();
    pokeAt = now + std::chrono::milliseconds(600);
    latePokeAt = now + std::chrono::milliseconds(3000);
  }

  bool due(Clock::time_point now) const {
    return now >= pokeAt || now >= latePokeAt;
  }

  void advance(Clock::time_point now) {
    const auto never = Clock::time_point::max();
    if (now >= pokeAt)
      pokeAt = never;
    if (now >= latePokeAt)
      latePokeAt = never;
  }
};

WindowFocusTracker::WindowFocusTracker(bool debugEnabled)
    : debugEnabled_(debugEnabled) {}

WindowFocusTracker::~WindowFocusTracker() { stop(); }

void WindowFocusTracker::connectionEstablished() {
  connectionFailureReported_.store(false, std::memory_order_relaxed);
  valid_.store(true, std::memory_order_relaxed);
  if (debugEnabled_.load(std::memory_order_relaxed)) {
    FCITX_INFO() << "areca: AT-SPI connected; focus events registered";
  }
}

void WindowFocusTracker::connectionLost() {
  if (valid_.exchange(false, std::memory_order_relaxed)) {
    connectionFailureReported_.store(true, std::memory_order_relaxed);
    if (debugEnabled_.load(std::memory_order_relaxed)) {
      FCITX_INFO() << "areca: AT-SPI connection lost";
    }
  }
  clearFocus();
}

void WindowFocusTracker::reportConnectionUnavailable(const char *reason) {
  if (connectionFailureReported_.exchange(true, std::memory_order_relaxed))
    return;
  if (debugEnabled_.load(std::memory_order_relaxed)) {
    FCITX_INFO() << "areca: AT-SPI unavailable: " << reason << "; retrying";
  }
}

void WindowFocusTracker::clearFocus() {
  focusBus_.clear();
  focusPath_.clear();
  focusInTerminal_.store(false);
  {
    std::lock_guard<std::mutex> lock(focusProgramMutex_);
    focusProgram_.clear();
    focusClass_.clear();
  }
}

void WindowFocusTracker::loseFocus(const char *sender, const char *path) {
  if (sender && focusBus_ == sender && (!path || focusPath_ == path))
    clearFocus();
}

bool WindowFocusTracker::start() {
  if (running_.load())
    return valid_.load();
  stopRequested_.store(false);
  valid_.store(false);
  thread_ = std::thread(&WindowFocusTracker::threadFunc, this);
  return true;
}

void WindowFocusTracker::stop() {
  stopRequested_.store(true);
  valid_.store(false);
  if (thread_.joinable())
    thread_.join();
}

bool WindowFocusTracker::registerEvents(DBusConnection *bus) {
  auto registerEvent = [&](const char *eventName) {
    DbusMessagePtr message(dbus_message_new_method_call(
        "org.a11y.atspi.Registry", "/org/a11y/atspi/registry",
        "org.a11y.atspi.Registry", "RegisterEvent"));
    if (!message)
      return false;
    dbus_message_append_args(message.get(), DBUS_TYPE_STRING, &eventName,
                             DBUS_TYPE_INVALID);

    DbusErrorGuard error;
    DbusMessagePtr reply(dbus_connection_send_with_reply_and_block(
        bus, message.get(), 2000, error.get()));
    return reply && !error.isSet();
  };

  return registerEvent("object:state-changed:focused") &&
         registerEvent("focus:");
}

void WindowFocusTracker::addEventMatches(DBusConnection *bus) {
  auto addMatch = [&](const char *rule) {
    DbusErrorGuard error;
    dbus_bus_add_match(bus, rule, error.get());
  };

  addMatch("type='signal',interface='org.a11y.atspi.Event.Object',"
           "member='StateChanged'");
  addMatch("type='signal',interface='org.a11y.atspi.Event.Focus'");
  addMatch("type='signal',sender='org.freedesktop.DBus',"
           "interface='org.freedesktop.DBus',member='NameOwnerChanged'");
}

void WindowFocusTracker::runConnection(DBusConnection *bus) {
  if (!registerEvents(bus)) {
    reportConnectionUnavailable("event registration failed");
    connectionLost();
    waitBeforeReconnect(20);
    return;
  }

  addEventMatches(bus);
  runEventLoop(bus);
  connectionLost();
  if (!stopRequested_.load())
    waitBeforeReconnect(10);
}

void WindowFocusTracker::runEventLoop(DBusConnection *bus) {
  AtspiAccessible accessible(bus);
  connectionEstablished();
  accessible.pokeApps();

  PokeSchedule schedule;
  while (!stopRequested_.load()) {
    // 100 ms đủ để dừng và poke kịp thời mà không thức dậy 33 lần mỗi giây.
    if (!dbus_connection_read_write(bus, kReadTimeoutMs))
      break;

    // Chuyển focus nhanh (Alt+Tab, click liên tục) sinh nhiều sự kiện liền
    // nhau. Hỏi chi tiết từng cái sẽ dồn hàng trên luồng này, nên chỉ xử lý
    // focus cuối cùng của lượt.
    PendingFocus pendingFocus;
    DbusMessagePtr message(dbus_connection_pop_message(bus));
    while (message) {
      processMessage(message.get(), schedule, pendingFocus);
      message.reset(dbus_connection_pop_message(bus));
    }
    if (pendingFocus.pending)
      processFocusEvent(bus, pendingFocus.sender, pendingFocus.path);

    if (pokeRequested_.exchange(false, std::memory_order_relaxed)) {
      accessible.pokeApps();
    }

    const auto now = PokeSchedule::Clock::now();
    if (schedule.due(now)) {
      schedule.advance(now);
      accessible.pokeApps();
    }
  }
}

void WindowFocusTracker::processMessage(DBusMessage *message,
                                        PokeSchedule &schedule,
                                        PendingFocus &pendingFocus) {
  const char *interface = dbus_message_get_interface(message);
  const char *member = dbus_message_get_member(message);
  if (!interface || !member)
    return;

  bool focusEvent = false;
  if (strcmp(interface, "org.a11y.atspi.Event.Object") == 0 &&
      strcmp(member, "StateChanged") == 0) {
    focusEvent = processStateChanged(message, pendingFocus);
  } else if (strcmp(interface, "org.a11y.atspi.Event.Focus") == 0) {
    focusEvent = true;
  } else if (strcmp(interface, "org.freedesktop.DBus") == 0 &&
             strcmp(member, "NameOwnerChanged") == 0) {
    processNameOwnerChanged(message, schedule, pendingFocus);
  }
  if (!focusEvent)
    return;

  const char *sender = dbus_message_get_sender(message);
  const char *path = dbus_message_get_path(message);
  if (sender && path) {
    pendingFocus.sender = sender;
    pendingFocus.path = path;
    pendingFocus.pending = true;
  }
}

bool WindowFocusTracker::processStateChanged(DBusMessage *message,
                                             PendingFocus &pendingFocus) {
  DBusMessageIter iter;
  if (!dbus_message_iter_init(message, &iter) ||
      dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING) {
    return false;
  }

  const char *stateName = nullptr;
  dbus_message_iter_get_basic(&iter, &stateName);
  if (!stateName || strcmp(stateName, "focused") != 0)
    return false;
  if (!dbus_message_iter_next(&iter) ||
      dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_INT32) {
    return false;
  }

  dbus_int32_t detail = 0;
  dbus_message_iter_get_basic(&iter, &detail);
  if (detail == 0) {
    const char *sender = dbus_message_get_sender(message);
    const char *path = dbus_message_get_path(message);
    loseFocus(sender, path);
    if (pendingFocus.pending && sender && path &&
        pendingFocus.sender == sender && pendingFocus.path == path) {
      pendingFocus.pending = false;
    }
  }
  return detail == 1;
}

void WindowFocusTracker::processNameOwnerChanged(DBusMessage *message,
                                                 PokeSchedule &schedule,
                                                 PendingFocus &pendingFocus) {
  const char *busName = nullptr;
  const char *oldOwner = nullptr;
  const char *newOwner = nullptr;
  DbusErrorGuard error;
  if (dbus_message_get_args(message, error.get(), DBUS_TYPE_STRING, &busName,
                            DBUS_TYPE_STRING, &oldOwner, DBUS_TYPE_STRING,
                            &newOwner, DBUS_TYPE_INVALID)) {
    if (oldOwner && oldOwner[0] && newOwner && !newOwner[0]) {
      loseFocus(busName);
      if (pendingFocus.pending && busName && pendingFocus.sender == busName)
        pendingFocus.pending = false;
    }
    if (newOwner && newOwner[0])
      schedule.applicationConnected();
  }
}

void WindowFocusTracker::processFocusEvent(DBusConnection *bus,
                                           const std::string &sender,
                                           const std::string &path) {
  clearFocus();
  focusBus_ = sender;
  focusPath_ = path;

  const AtspiAccessible accessible(bus);
  // Tên chương trình chỉ cần bus daemon và /proc nên có ngay. Công bố trước để
  // phím gõ trong lúc còn hỏi ứng dụng chi tiết (role, terminal) đã thấy đúng
  // app thay vì chuỗi rỗng.
  const int processId = accessible.senderProcessId(sender.c_str());
  const std::string program = AtspiAccessible::programForProcess(processId);
  if (AtspiAccessible::isWindowManagerProgram(program)) {
    clearFocus();
    return;
  }
  {
    std::lock_guard<std::mutex> lock(focusProgramMutex_);
    focusProgram_ = program;
  }

  const AtspiFocusInfo info =
      accessible.inspectFocus(sender.c_str(), path.c_str(), processId);
  if (info.ignoredWindowManager) {
    clearFocus();
    return;
  }
  focusInTerminal_.store(info.terminal, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(focusProgramMutex_);
    focusProgram_ = info.program;
    focusClass_ = info.className;
  }
}

void WindowFocusTracker::waitBeforeReconnect(int intervals) {
  for (int i = 0; i < intervals && !stopRequested_.load(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

void WindowFocusTracker::threadFunc() {
  running_.store(true);
  while (!stopRequested_.load()) {
    clearFocus();
    AtspiConnection connection;
    DBusConnection *bus = connection.get();
    if (!bus) {
      reportConnectionUnavailable("bus connection failed");
      connectionLost();
      waitBeforeReconnect(20);
      continue;
    }
    runConnection(bus);
  }

  connectionLost();
  running_.store(false);
}

} // namespace areca
