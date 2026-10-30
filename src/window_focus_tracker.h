#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace areca {

class WindowFocusTracker {
public:
  WindowFocusTracker();
  ~WindowFocusTracker();

  WindowFocusTracker(const WindowFocusTracker &) = delete;
  WindowFocusTracker &operator=(const WindowFocusTracker &) = delete;

  static bool isAvailable();

  bool start();
  void stop();

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
  void clearFocus();
  void loseFocus(const char *sender, const char *path = nullptr);
  void threadFunc();

  // Owned by the monitor thread.
  std::string focusBus_;
  std::string focusPath_;

  std::thread thread_;
  std::atomic<bool> valid_{false};
  std::atomic<bool> running_{false};
  std::atomic<bool> stopRequested_{false};
  std::atomic<bool> focusInTerminal_{false};

  mutable std::mutex focusProgramMutex_;
  std::string focusProgram_;
  std::string focusClass_;
};

} // namespace areca
