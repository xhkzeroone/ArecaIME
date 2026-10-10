#pragma once

#include <cstdint>
#include <functional>
#include <string>

typedef struct _XDisplay Display;

namespace areca {

class XTestBackspaceDevice {
public:
  virtual ~XTestBackspaceDevice() = default;
  virtual bool isAvailable() = 0;
  virtual bool sendBackspace() = 0;
  virtual bool sendShift(bool press) { (void)press; return false; }
  virtual bool sendLeft() { return false; }
};

class XTestDevice final : public XTestBackspaceDevice {
public:
  using DebugProvider = std::function<bool()>;

  explicit XTestDevice(DebugProvider debugProvider);
  ~XTestDevice() override;

  XTestDevice(const XTestDevice &) = delete;
  XTestDevice &operator=(const XTestDevice &) = delete;
  XTestDevice(XTestDevice &&) = delete;
  XTestDevice &operator=(XTestDevice &&) = delete;

  bool isAvailable() override;
  bool ensureDevice(const char *displayName = nullptr);
  bool warmUp();
  void closeDevice();
  bool sendKey(uint32_t keysym, bool press);
  bool sendBackspace() override;
  bool sendShift(bool press) override;
  bool sendLeft() override;

private:
  DebugProvider debugProvider_;
  Display *display_ = nullptr;
  std::string activeDisplayName_;
  bool initialized_ = false;
  bool xtestSupported_ = false;
  uint32_t backspaceKeycode_ = 0;
  uint32_t shiftKeycode_ = 0;
  uint32_t leftKeycode_ = 0;
};

} // namespace areca
