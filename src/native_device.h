#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include <fcitx-utils/dbus/bus.h>
#include <fcitx-utils/event.h>

#include "native_keyboard_device.h"

namespace areca {

// Bàn phím Native chỉ dùng libei qua phiên RemoteDesktop portal chỉ xin quyền
// bàn phím. Không có Wayland, libei hay portal từ chối thì không khả dụng và bộ
// chọn backend dùng uinput.
class NativeDevice final : public NativeKeyboardDevice {
public:
  using DebugProvider = std::function<bool()>;
  // Bus session của fcitx5 để hỏi portal; trả nullptr khi không có.
  using BusProvider = std::function<fcitx::dbus::Bus *()>;

  NativeDevice(fcitx::EventLoop &eventLoop, DebugProvider debugProvider,
               BusProvider busProvider);
  ~NativeDevice() override;

  NativeDevice(const NativeDevice &) = delete;
  NativeDevice &operator=(const NativeDevice &) = delete;

  bool isAvailable() override;
  bool sendBackspace() override;
  bool sendShift(bool press) override;
  bool sendLeft() override;
  // Hỏi portal có RemoteDesktop kèm ConnectToEIS và bàn phím không, chỉ đọc
  // thuộc tính nên không mở hộp thoại xin quyền. Kết quả về bất đồng bộ.
  void probeSupport();
  // Có thể dùng được hay không mà không xin quyền: false khi bản build không
  // có libei, phiên không phải Wayland, hoặc portal đã từ chối.
  bool isSupported() const;

private:
  bool sendKey(uint32_t keycode, bool press);
  bool tapKey(uint32_t keycode);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace areca
