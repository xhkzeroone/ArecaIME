#include "uinput_device.h"

#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <fcitx-utils/log.h>

namespace areca {

UinputDevice::UinputDevice(DebugProvider debugProvider)
    : debugProvider_(std::move(debugProvider)) {}

UinputDevice::~UinputDevice() { closeDevice(); }

bool UinputDevice::isAvailable() { return ensureDevice(); }

bool UinputDevice::ensureDevice() {
  if (deviceInitialized_) {
    return uinputFd_ >= 0;
  }
  deviceInitialized_ = true;

  uinputFd_ = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (uinputFd_ < 0) {
    uinputFd_ = open("/dev/input/uinput", O_WRONLY | O_NONBLOCK);
  }
  if (uinputFd_ < 0) {
    if (debugProvider_()) {
      FCITX_INFO() << "areca: uinput failed to open /dev/uinput";
    }
    return false;
  }

  if (ioctl(uinputFd_, UI_SET_EVBIT, EV_KEY) < 0 ||
      ioctl(uinputFd_, UI_SET_KEYBIT, KEY_LEFTSHIFT) < 0 ||
      ioctl(uinputFd_, UI_SET_KEYBIT, KEY_LEFT) < 0 ||
      ioctl(uinputFd_, UI_SET_KEYBIT, KEY_BACKSPACE) < 0 ||
      ioctl(uinputFd_, UI_SET_EVBIT, EV_SYN) < 0) {
    closeDevice();
    return false;
  }

#ifdef UI_DEV_SETUP
  struct uinput_setup usetup;
  std::memset(&usetup, 0, sizeof(usetup));
  usetup.id.bustype = BUS_USB;
  usetup.id.vendor = 0x1234;
  usetup.id.product = 0x5678;
  std::strncpy(usetup.name, "ArecaIME Virtual Keyboard",
               UINPUT_MAX_NAME_SIZE - 1);
  if (ioctl(uinputFd_, UI_DEV_SETUP, &usetup) < 0 ||
      ioctl(uinputFd_, UI_DEV_CREATE) < 0) {
#endif
    struct uinput_user_dev udev;
    std::memset(&udev, 0, sizeof(udev));
    std::strncpy(udev.name, "ArecaIME Virtual Keyboard",
                 UINPUT_MAX_NAME_SIZE - 1);
    udev.id.bustype = BUS_USB;
    udev.id.vendor = 0x1234;
    udev.id.product = 0x5678;
    if (write(uinputFd_, &udev, sizeof(udev)) < 0 ||
        ioctl(uinputFd_, UI_DEV_CREATE) < 0) {
      closeDevice();
      return false;
    }
#ifdef UI_DEV_SETUP
  }
#endif

  if (debugProvider_()) {
    FCITX_INFO() << "areca: uinput device initialized successfully (fd="
                 << uinputFd_ << ")";
  }
  return true;
}

void UinputDevice::closeDevice() {
  if (uinputFd_ >= 0) {
    ioctl(uinputFd_, UI_DEV_DESTROY);
    close(uinputFd_);
    uinputFd_ = -1;
  }
}

void UinputDevice::sendKeyEvent(uint16_t code, int value) {
  if (uinputFd_ < 0) {
    return;
  }

  struct input_event ev;
  std::memset(&ev, 0, sizeof(ev));
  ev.type = EV_KEY;
  ev.code = code;
  ev.value = value;
  (void)write(uinputFd_, &ev, sizeof(ev));

  std::memset(&ev, 0, sizeof(ev));
  ev.type = EV_SYN;
  ev.code = SYN_REPORT;
  ev.value = 0;
  (void)write(uinputFd_, &ev, sizeof(ev));
}

} // namespace areca
