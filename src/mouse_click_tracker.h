#pragma once

#include <fcitx-utils/event.h>
#include <memory>
#include <functional>
#include <utility>
#include <sys/types.h>

namespace areca {

// All methods run on the Fcitx event loop; the helper owns libinput.
class MouseClickTracker {
public:
  explicit MouseClickTracker(fcitx::EventLoop &loop,
                             std::function<bool()> debug = {})
      : loop_(loop), debug_(std::move(debug)) {}
  ~MouseClickTracker();
  MouseClickTracker(const MouseClickTracker &) = delete;
  MouseClickTracker &operator=(const MouseClickTracker &) = delete;
  bool start();
  void stop();
  bool isValid() const { return ready_; }
  bool hasPendingClick();
  void clearPendingClick() { pending_ = false; }

private:
  void drain();
  bool debugEnabled() const { return debug_ && debug_(); }
  fcitx::EventLoop &loop_;
  std::function<bool()> debug_;
  std::unique_ptr<fcitx::EventSourceIO> source_;
  pid_t child_ = -1;
  int fd_ = -1;
  bool ready_ = false;
  bool pending_ = false;
};

} // namespace areca
