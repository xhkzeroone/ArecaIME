#pragma once

#include <fcitx-utils/event.h>
#include <memory>
#include <functional>
#include <utility>
#include <sys/types.h>

namespace areca {

// Helper riêng đọc libinput; lớp này chỉ nhận thông báo trên event loop Fcitx.
// Mọi phương thức chạy cùng event loop nên không cần thread, mutex hay atomic;
// việc reset engine thuộc về ArecaEngine để tuân theo trạng thái scheduler.
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
  // Đã nhận tín hiệu khởi tạo libinput, không đảm bảo có thiết bị đọc được.
  bool isValid() const { return ready_; }
  // Đọc cờ không tiêu thụ click: scheduler có thể cần hoãn reset sang phím sau.
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
