#pragma once

#include <functional>
#include <memory>

#include <fcitx-utils/event.h>

#include "xtest_device.h"

namespace areca {

// Keyboard injection policy used by the Native rewrite backend:
//   * Wayland: keyboard-only RemoteDesktop portal + libei.
//   * X11, or a failed/unavailable libei session: XTest.
class NativeDevice final : public XTestBackspaceDevice {
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
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace areca
