#pragma once

#include <cstdint>
#include <functional>

namespace areca {

class UinputDevice {
public:
  using DebugProvider = std::function<bool()>;
  using UnavailableCallback = std::function<void()>;

  explicit UinputDevice(
      DebugProvider debugProvider,
      UnavailableCallback unavailableCallback = {});
  virtual ~UinputDevice();

  bool isAvailable();
  virtual bool ensureDevice();
  void closeDevice();
  virtual void sendKeyEvent(uint16_t code, int value);

private:
  DebugProvider debugProvider_;
  UnavailableCallback unavailableCallback_;
  int uinputFd_ = -1;
  bool deviceInitialized_ = false;
};

} // namespace areca
