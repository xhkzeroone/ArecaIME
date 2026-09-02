#pragma once

#include <cstdint>
#include <functional>

namespace areca {

class UinputDevice {
public:
  using DebugProvider = std::function<bool()>;

  explicit UinputDevice(DebugProvider debugProvider);
  ~UinputDevice();

  bool isAvailable();
  bool ensureDevice();
  void closeDevice();
  void sendKeyEvent(uint16_t code, int value);

private:
  DebugProvider debugProvider_;
  int uinputFd_ = -1;
  bool deviceInitialized_ = false;
};

} // namespace areca
