#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include <fcitx-utils/event.h>

#include "native_keyboard_device.h"

namespace areca {

// Bàn phím Native chỉ dùng libei qua phiên RemoteDesktop portal chỉ xin quyền
// bàn phím. Không có Wayland, libei hay portal từ chối thì không khả dụng và bộ
// chọn backend dùng uinput.
class NativeDevice final : public NativeKeyboardDevice {
public:
  using DebugProvider = std::function<bool()>;

  NativeDevice(fcitx::EventLoop &eventLoop, DebugProvider debugProvider);
  ~NativeDevice() override;

  NativeDevice(const NativeDevice &) = delete;
  NativeDevice &operator=(const NativeDevice &) = delete;

  bool isAvailable() override;
  bool sendBackspace() override;
  bool sendShift(bool press) override;
  bool sendLeft() override;
  bool warmUp();

private:
  bool sendKey(uint32_t keycode, bool press);
  bool tapKey(uint32_t keycode);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace areca
