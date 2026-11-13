#pragma once

#include <cstdint>
#include <optional>
#include <utility>

namespace areca {

// Nhận ra Backspace bị frontend gửi lại sau khi Areca đã chuyển tiếp nó.
//
// GNOME/IBus gửi lại đúng press vừa chuyển tiếp, cùng timestamp và keycode,
// chỉ vài ms sau. Frontend Wayland khi giữ phím lại tự sinh phím lặp mang
// timestamp của lần nhấn gốc, nhưng phím lặp đầu tới sau độ trễ lặp phím
// (≥ 100 ms) và các phím sau không phải bản gửi lại. Vì vậy mỗi press chỉ
// được coi là gửi lại tối đa một lần và chỉ khi tới trong ReplayWindowUsec;
// press lặp cùng timestamp với press trước không mở cửa sổ mới.
class BackspaceReplayGuard {
public:
  // Ngắn hơn mọi độ trễ lặp phím thực tế, dài hơn độ trễ gửi lại của GNOME.
  static constexpr uint64_t ReplayWindowUsec = 50 * 1000;

  // Areca vừa chuyển tiếp một press Backspace cho ứng dụng.
  void forwarded(int time, uint32_t keycode, uint64_t nowUsec) {
    holding_ = true;
    const std::pair<int, uint32_t> key{time, keycode};
    if (time != 0 && key != lastForwarded_) {
      candidate_ = Candidate{key, nowUsec};
    } else {
      candidate_.reset();
    }
    lastForwarded_ = key;
  }

  // true nếu press này là bản gửi lại của press vừa chuyển tiếp. Cửa sổ chỉ
  // dùng được một lần, dù press có khớp hay không.
  bool takeReplay(int time, uint32_t keycode, uint64_t nowUsec) {
    const auto candidate = std::exchange(candidate_, std::nullopt);
    return candidate && time != 0 &&
           candidate->key == std::pair<int, uint32_t>{time, keycode} &&
           nowUsec - candidate->forwardedAtUsec <= ReplayWindowUsec;
  }

  // Đang trong một lượt giữ Backspace đã được chuyển tiếp.
  bool holding() const { return holding_; }

  void clear() {
    holding_ = false;
    candidate_.reset();
    lastForwarded_ = {};
  }

private:
  struct Candidate {
    std::pair<int, uint32_t> key;
    uint64_t forwardedAtUsec;
  };

  bool holding_ = false;
  std::optional<Candidate> candidate_;
  std::pair<int, uint32_t> lastForwarded_;
};

} // namespace areca
