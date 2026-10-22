#pragma once

#include <cstdint>
#include <map>
#include <utility>

namespace areca {

// Đếm một loại phím quay lại Fcitx sau khi được phát qua uinput. N lần đầu
// được forward để thực hiện thao tác thật; lần N+1 chỉ xác nhận hàng đợi đã đi
// hết nên phải bị lọc.
class UinputKeyAckTracker {
public:
  enum class PressAction {
    Forward,
    Filter,
    FilterAndAcknowledge,
  };

  void reset(uint32_t forwardedPresses) {
    forwardedPresses_ = forwardedPresses;
    expectedPresses_ = forwardedPresses + 1;
    pressesSeen_ = 0;
    releasesSeen_ = 0;
    completionSignaled_ = false;
    pressActions_.clear();
    releaseActions_.clear();
  }

  void clear() {
    forwardedPresses_ = 0;
    expectedPresses_ = 0;
    pressesSeen_ = 0;
    releasesSeen_ = 0;
    completionSignaled_ = false;
    pressActions_.clear();
    releaseActions_.clear();
  }

  PressAction observePress() {
    if (pressesSeen_ < expectedPresses_) {
      ++pressesSeen_;
    }
    if (pressesSeen_ <= forwardedPresses_) {
      return PressAction::Forward;
    }
    if (!completionSignaled_ && pressesSeen_ == expectedPresses_) {
      completionSignaled_ = true;
      return PressAction::FilterAndAcknowledge;
    }
    return PressAction::Filter;
  }

  bool shouldFilterRelease() {
    if (releasesSeen_ < expectedPresses_) {
      ++releasesSeen_;
    }
    return releasesSeen_ > forwardedPresses_;
  }

  // GTK có thể xử lý lại event chưa được IM nhận. Không tính lượt gửi lại
  // thành phím xác nhận và lọc bỏ để tránh gửi lặp phím xóa xuống app.
  // Timestamp 0 không đủ để nhận diện event.
  PressAction observePress(uint32_t time, uint32_t code, bool repeat) {
    if (!time || !code) {
      return observePress();
    }
    const auto id = std::make_pair(time, code);
    if (repeat) {
      if (pressActions_.find(id) != pressActions_.end()) {
        return PressAction::Filter;
      }
    }
    const auto action = observePress();
    pressActions_[id] = action;
    // Hai phím thật có thể cùng timestamp do độ phân giải mili giây.
    releaseActions_.erase(id);
    return action;
  }

  bool shouldFilterRelease(uint32_t time, uint32_t code) {
    if (!time || !code) {
      return shouldFilterRelease();
    }
    const auto id = std::make_pair(time, code);
    if (releaseActions_.find(id) != releaseActions_.end()) {
      return true;
    }
    const bool filter = shouldFilterRelease();
    releaseActions_[id] = filter;
    return filter;
  }

  uint32_t expectedPresses() const { return expectedPresses_; }
  uint32_t pressesSeen() const { return pressesSeen_; }
  uint32_t releasesSeen() const { return releasesSeen_; }

private:
  uint32_t forwardedPresses_ = 0;
  uint32_t expectedPresses_ = 0;
  uint32_t pressesSeen_ = 0;
  uint32_t releasesSeen_ = 0;
  bool completionSignaled_ = false;
  std::map<std::pair<uint32_t, uint32_t>, PressAction> pressActions_;
  std::map<std::pair<uint32_t, uint32_t>, bool> releaseActions_;
};

} // namespace areca
