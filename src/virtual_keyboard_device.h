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
  virtual bool sendKey(uint32_t evdevKeycode, bool press);
  // Gọi callback trên event loop khi compositor đã nhận hết các request trước.
  virtual bool sync(SyncCallback callback);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace areca
