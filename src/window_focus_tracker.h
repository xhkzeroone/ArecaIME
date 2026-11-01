#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

typedef struct DBusConnection DBusConnection;
typedef struct DBusMessage DBusMessage;

namespace areca {

class WindowFocusTracker {
public:
  explicit WindowFocusTracker(bool debugEnabled = false);
  ~WindowFocusTracker();

  WindowFocusTracker(const WindowFocusTracker &) = delete;
  WindowFocusTracker &operator=(const WindowFocusTracker &) = delete;

  static bool isAvailable();

  bool start();
  void stop();
  void requestAccessibilityPoke() {
    pokeRequested_.store(true, std::memory_order_relaxed);
  }
  void setDebugEnabled(bool enabled) {
    debugEnabled_.store(enabled, std::memory_order_relaxed);
  }

  bool isValid() const {
    return valid_.load(std::memory_order_relaxed);
  }

  struct BrowserFocus {
    std::string program;
    std::string className;
  };

  BrowserFocus browserFocus() const {
    std::lock_guard<std::mutex> lock(focusProgramMutex_);
    return {focusProgram_, focusClass_};
  }

  bool isTerminalFocused() const {
    return focusInTerminal_.load(std::memory_order_relaxed);
  }

  std::string focusProgram() const {
    std::lock_guard<std::mutex> lock(focusProgramMutex_);
    return focusProgram_;
  }

private:
  friend struct WindowFocusTrackerTestAccess;
  void connectionEstablished();
  void connectionLost();
  void reportConnectionUnavailable(const char *reason);
  void clearFocus();
  void loseFocus(const char *sender, const char *path = nullptr);
  struct PokeSchedule;
  bool registerEvents(DBusConnection *bus);
  void addEventMatches(DBusConnection *bus);
  void runConnection(DBusConnection *bus);
  void runEventLoop(DBusConnection *bus);
  void processMessage(DBusConnection *bus, DBusMessage *message,
                      PokeSchedule &schedule);
  bool processStateChanged(DBusMessage *message);
  void processNameOwnerChanged(DBusMessage *message, PokeSchedule &schedule);
  void processFocusEvent(DBusConnection *bus, DBusMessage *message);
  void waitBeforeReconnect(int intervals);
  void threadFunc();

  // Owned by the monitor thread.
  std::string focusBus_;
  std::string focusPath_;

  std::thread thread_;
  std::atomic<bool> valid_{false};
  std::atomic<bool> running_{false};
  std::atomic<bool> stopRequested_{false};
  std::atomic<bool> pokeRequested_{false};
  std::atomic<bool> debugEnabled_{false};
  std::atomic<bool> connectionFailureReported_{false};
  std::atomic<bool> focusInTerminal_{false};

  mutable std::mutex focusProgramMutex_;
  std::string focusProgram_;
  std::string focusClass_;
};

} // namespace areca
