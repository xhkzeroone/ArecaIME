#pragma once

namespace areca {

// Bàn phím mô phỏng mà các backend Native dùng để bấm Backspace, Shift và
// Left. Tách thành interface để test thay bằng thiết bị giả.
class NativeKeyboardDevice {
public:
  virtual ~NativeKeyboardDevice() = default;
  virtual bool isAvailable() = 0;
  virtual bool sendBackspace() = 0;
  virtual bool sendShift(bool press) {
    (void)press;
    return false;
  }
  virtual bool sendLeft() { return false; }
};

} // namespace areca
