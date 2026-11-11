#include "virtual_keyboard_device.h"

#ifdef ARECA_HAVE_WAYLAND_VK

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>

#include <sys/mman.h>
#include <unistd.h>

#include <fcitx-utils/log.h>
#include <wayland-client.h>

#include "serial_worker.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"

namespace areca {
namespace {

// evdev KEY_BACKSPACE; khớp với keymap của buildVirtualKeyboardKeymap.
constexpr uint32_t kBackspaceKeycode = 14;

struct Globals {
  wl_seat *seat = nullptr;
  zwp_virtual_keyboard_manager_v1 *manager = nullptr;
};

void handleGlobal(void *data, wl_registry *registry, uint32_t name,
                  const char *interface, uint32_t /*version*/) {
  auto *globals = static_cast<Globals *>(data);
  if (std::strcmp(interface, wl_seat_interface.name) == 0 && !globals->seat) {
    globals->seat = static_cast<wl_seat *>(
        wl_registry_bind(registry, name, &wl_seat_interface, 1));
  } else if (std::strcmp(interface,
                         zwp_virtual_keyboard_manager_v1_interface.name) == 0 &&
             !globals->manager) {
    globals->manager =
        static_cast<zwp_virtual_keyboard_manager_v1 *>(wl_registry_bind(
            registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1));
  }
}

void handleGlobalRemove(void *, wl_registry *, uint32_t) {}

const wl_registry_listener kRegistryListener = {handleGlobal,
                                                handleGlobalRemove};

// Một kết nối Wayland riêng kèm thiết bị phím ảo. Chỉ dùng trên một thread.
class Connection final {
public:
  ~Connection() { close(); }

  bool open() {
    display_ = wl_display_connect(nullptr);
    if (!display_) {
      return false;
    }
    registry_ = wl_display_get_registry(display_);
    wl_registry_add_listener(registry_, &kRegistryListener, &globals_);
    if (wl_display_roundtrip(display_) < 0 || !globals_.seat ||
        !globals_.manager) {
      close();
      return false;
    }
    return true;
  }

  bool hasDevice() const { return keyboard_ != nullptr; }

  bool createDevice() {
    keyboard_ = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(
        globals_.manager, globals_.seat);
    return keyboard_ != nullptr;
  }

  bool uploadKeymap(const std::string &keymap) {
    const int fd = memfd_create("areca-virtual-keyboard", MFD_CLOEXEC);
    if (fd < 0) {
      return false;
    }
    const size_t size = keymap.size() + 1;
    bool ok = ftruncate(fd, static_cast<off_t>(size)) == 0;
    size_t written = 0;
    while (ok && written < size) {
      const ssize_t count = write(fd, keymap.c_str() + written, size - written);
      if (count < 0 && errno == EINTR) {
        continue;
      }
      ok = count > 0;
      written += ok ? static_cast<size_t>(count) : 0;
    }
    if (ok) {
      zwp_virtual_keyboard_v1_keymap(keyboard_,
                                     WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd,
                                     static_cast<uint32_t>(size));
      zwp_virtual_keyboard_v1_modifiers(keyboard_, 0, 0, 0, 0);
    }
    ::close(fd);
    return ok;
  }

  void tap(uint32_t evdevKeycode) {
    zwp_virtual_keyboard_v1_key(keyboard_, time_++, evdevKeycode,
                                WL_KEYBOARD_KEY_STATE_PRESSED);
    zwp_virtual_keyboard_v1_key(keyboard_, time_++, evdevKeycode,
                                WL_KEYBOARD_KEY_STATE_RELEASED);
  }

  // Chờ compositor nhận hết các request trước đó.
  bool sync() { return wl_display_roundtrip(display_) >= 0; }

  void close() {
    if (keyboard_) {
      zwp_virtual_keyboard_v1_destroy(keyboard_);
      keyboard_ = nullptr;
    }
    if (globals_.manager) {
      zwp_virtual_keyboard_manager_v1_destroy(globals_.manager);
      globals_.manager = nullptr;
    }
    if (globals_.seat) {
      wl_seat_destroy(globals_.seat);
      globals_.seat = nullptr;
    }
    if (registry_) {
      wl_registry_destroy(registry_);
      registry_ = nullptr;
    }
    if (display_) {
      wl_display_disconnect(display_);
      display_ = nullptr;
    }
  }

private:
  wl_display *display_ = nullptr;
  wl_registry *registry_ = nullptr;
  Globals globals_;
  zwp_virtual_keyboard_v1 *keyboard_ = nullptr;
  uint32_t time_ = 1;
};

class WaylandVirtualKeyboard final : public VirtualKeyboardDevice {
public:
  ~WaylandVirtualKeyboard() override {
    // Dừng thread trước: kết nối chỉ được đụng tới từ thread đó.
    worker_.stop();
  }

  const char *kind() const override { return "wayland"; }

  bool isAvailable() override {
    if (!probed_) {
      probed_ = true;
      const char *display = std::getenv("WAYLAND_DISPLAY");
      // KWin và Mutter không cấp phím ảo cho kết nối thường; chỉ compositor
      // kiểu wlroots mới có global này.
      Connection probe;
      available_ = display && display[0] && probe.open();
    }
    return available_;
  }

  void replace(const VirtualKeyboardRequest &request, Done done) override {
    worker_.post([this, request, done = std::move(done)] {
      const bool ok = emit(request);
      if (!ok) {
        // Kết nối hỏng thì mở lại ở lần sau.
        connection_.close();
      }
      done(ok);
    });
  }

  void cancel() override { worker_.dropPending(); }

private:
  bool ensureDevice() {
    if (connection_.hasDevice()) {
      return true;
    }
    connection_.close();
    return connection_.open() && connection_.createDevice();
  }

  bool emit(const VirtualKeyboardRequest &request) {
    const auto codepoints = utf8Codepoints(request.text);
    if (!request.text.empty() && codepoints.empty()) {
      return false;
    }
    if (!ensureDevice()) {
      FCITX_ERROR() << "areca: virtual-keyboard wayland device unavailable";
      return false;
    }
    // Lượt đầu luôn có keymap (kể cả khi chỉ xoá) để Backspace có keysym.
    size_t position = 0;
    bool first = true;
    while (first || position < codepoints.size()) {
      const size_t count =
          std::min(kVirtualKeyboardTextKeys, codepoints.size() - position);
      const std::vector<uint32_t> chunk(
          codepoints.begin() + static_cast<std::ptrdiff_t>(position),
          codepoints.begin() + static_cast<std::ptrdiff_t>(position + count));
      if (!connection_.uploadKeymap(buildVirtualKeyboardKeymap(chunk)) ||
          !connection_.sync()) {
        return false;
      }
      if (first) {
        for (uint32_t i = 0; i < request.deleteCount; ++i) {
          connection_.tap(kBackspaceKeycode);
        }
        first = false;
      }
      for (size_t i = 0; i < count; ++i) {
        connection_.tap(virtualKeyboardTextKeycode(i));
      }
      if (!connection_.sync()) {
        return false;
      }
      position += count;
    }
    return true;
  }

  SerialWorker worker_;
  Connection connection_;
  bool probed_ = false;
  bool available_ = false;
};

} // namespace

std::unique_ptr<VirtualKeyboardDevice> createWaylandVirtualKeyboard() {
  return std::make_unique<WaylandVirtualKeyboard>();
}

} // namespace areca

#else

namespace areca {

std::unique_ptr<VirtualKeyboardDevice> createWaylandVirtualKeyboard() {
  return nullptr;
}

} // namespace areca

#endif
