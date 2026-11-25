#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <fcitx-utils/event.h>

namespace areca {

// Bàn phím ảo Wayland (zwp_virtual_keyboard_v1) qua một kết nối Wayland riêng,
// chạy trên event loop. Kết nối được dò bất đồng bộ; trước khi dò xong, hoặc
// khi compositor không cấp giao thức này, thiết bị báo không khả dụng. Hàm
// virtual để test thay bằng thiết bị giả.
class VirtualKeyboardDevice {
public:
  using DebugProvider = std::function<bool()>;
  using SyncCallback = std::function<void(bool ok)>;

  VirtualKeyboardDevice(fcitx::EventLoop &eventLoop,
                        DebugProvider debugProvider);
  virtual ~VirtualKeyboardDevice();

  VirtualKeyboardDevice(const VirtualKeyboardDevice &) = delete;
  VirtualKeyboardDevice &operator=(const VirtualKeyboardDevice &) = delete;

  bool warmUp();
  virtual bool isAvailable();
  // Keymap XKB áp cho các phím gửi sau đó.
  virtual bool setKeymap(const std::string &keymap);
  // Trạng thái khoá (bit CapsLock, NumLock) gửi kèm mỗi keymap. Gửi 0 làm
  // compositor tắt đèn NumLock của bàn phím thật mỗi lần phím ảo gõ.
  virtual void setLockedModifiers(uint32_t mask);
  // Gửi các phím bổ trợ đang giữ (Shift, Ctrl, Alt, Super) sau khi gõ xong,
  // để ứng dụng không tưởng chúng đã nhả.
  virtual bool sendHeldModifiers(uint32_t mask);
  // `timeMs` là thời điểm gắn vào phím; ứng dụng và fcitx5 nhận lại đúng số
  // này nên dùng nó để nhận ra phím của chính Areca.
  virtual bool sendKey(uint32_t evdevKeycode, bool press, uint32_t timeMs);
  // Gọi callback trên event loop khi compositor đã nhận hết các request trước.
  virtual bool sync(SyncCallback callback);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace areca
