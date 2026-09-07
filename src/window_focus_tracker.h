#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
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

  bool isBrowserUIFocused() const {
    return browserUIFocused_.load(std::memory_order_relaxed);
  }

  bool isWebContentFocused() const {
    return focusInWebDoc_.load(std::memory_order_relaxed);
  }

  bool isTerminalFocused() const {
    return focusInTerminal_.load(std::memory_order_relaxed);
  }

  int focusRole() const {
    return focusRole_.load(std::memory_order_relaxed);
  }

  bool isFocusSnapshotFresh(uint64_t maxAgeUsec) const {
    uint64_t snap = focusSnapshotUsec_.load(std::memory_order_relaxed);
    if (snap == 0)
      return false;
    uint64_t nowUsec = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count() / 1000);
    return nowUsec - snap <= maxAgeUsec;
  }

  bool isPasswordFocused() const {
    return passwordFocused_.load(std::memory_order_relaxed);
  }

  int focusProcessId() const {
    return focusProcessId_.load(std::memory_order_relaxed);
  }

  std::string focusProgram() const {
    std::lock_guard<std::mutex> lock(focusProgramMutex_);
    return focusProgram_;
  }

  bool isTextEntryFocused() const {
    return textEntryFocused_.load(std::memory_order_relaxed);
  }

  bool isFocusEditable() const {
    return focusEditable_.load(std::memory_order_relaxed);
  }

  bool isFocusSingleLine() const {
    return focusSingleLine_.load(std::memory_order_relaxed);
  }

  bool isFocusMultiline() const {
    return focusMultiline_.load(std::memory_order_relaxed);
  }

  bool focusedTextEntry(std::string &busName, std::string &path,
                        uint64_t &snapshotUsec) const;

  bool a11yState(std::string &text, int &selStart, int &selEnd,
                 uint64_t maxAgeUsec) const;

  void setPollingEnabled(bool enabled) {
    pollEnabled_.store(enabled, std::memory_order_relaxed);
  }

  void waitForSnapshotUpdate(uint64_t timeoutUsec) const;

  static std::string atspiBusAddress();

  bool isRunning() const {
    return running_.load(std::memory_order_relaxed);
  }

  void setDebug(bool enabled) {
    debug_.store(enabled, std::memory_order_relaxed);
  }

private:
  void threadFunc();

  std::thread thread_;
  std::atomic<bool> valid_{false};
  std::atomic<bool> running_{false};
  std::atomic<bool> stopRequested_{false};
  std::atomic<bool> browserUIFocused_{false};
  std::atomic<bool> passwordFocused_{false};
  std::atomic<bool> debug_{false};
  std::atomic<bool> focusInWebDoc_{false};
  std::atomic<bool> focusInTerminal_{false};
  std::atomic<int> focusRole_{0};
  std::atomic<bool> focusEditable_{false};
  std::atomic<int> focusProcessId_{-1};
  std::atomic<bool> focusMultiline_{false};
  std::atomic<bool> focusSingleLine_{false};
  std::atomic<bool> textEntryFocused_{false};
  std::atomic<uint64_t> focusSnapshotUsec_{0};

  mutable std::mutex focusProgramMutex_;
  std::string focusProgram_;

  mutable std::mutex focusEntryMutex_;
  std::string focusEntryBus_;
  std::string focusEntryPath_;
  std::atomic<uint64_t> focusEntrySnapshotUsec_{0};

  mutable std::mutex a11ySnapshotMutex_;
  std::string a11ySnapshotText_;
  int a11ySnapshotSelStart_ = -1;
  int a11ySnapshotSelEnd_ = -1;
  uint64_t a11ySnapshotUsec_ = 0;
  uint64_t lastA11yPollUsec_ = 0;
  bool a11yPollDirty_ = false;
  std::atomic<bool> pollEnabled_{false};
  mutable std::condition_variable snapshotCv_;
};

} // namespace areca
