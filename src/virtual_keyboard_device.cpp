#include "virtual_keyboard_device.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string_view>
#include <utility>

#include <fcitx-utils/log.h>

#ifdef ARECA_HAVE_WAYLAND_VK
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include "virtual-keyboard-unstable-v1-client-protocol.h"
#endif

namespace areca {

struct VirtualKeyboardDevice::Impl {
  enum class State { Idle, Pending, Ready, Failed };

  Impl(fcitx::EventLoop &eventLoop, DebugProvider provider)
      : eventLoop(eventLoop), debugProvider(std::move(provider)) {}

  ~Impl() {
#ifdef ARECA_HAVE_WAYLAND_VK
    teardownSource.reset();
    displaySource.reset();
    disconnect();
#endif
  }

  fcitx::EventLoop &eventLoop;
  DebugProvider debugProvider;
  State state = State::Idle;

  void log(const char *message) const {
    if (debugProvider()) {
      FCITX_INFO() << "areca: virtual-keyboard-device " << message;
    }
  }

#ifdef ARECA_HAVE_WAYLAND_VK
  struct PendingSync {
    wl_callback *callback;
    SyncCallback done;
  };

  wl_display *display = nullptr;
  wl_registry *registry = nullptr;
  wl_seat *seat = nullptr;
  zwp_virtual_keyboard_manager_v1 *manager = nullptr;
  zwp_virtual_keyboard_v1 *keyboard = nullptr;
  wl_callback *probeCallback = nullptr;
  std::deque<PendingSync> syncs;
  std::unique_ptr<fcitx::EventSourceIO> displaySource;
  std::unique_ptr<fcitx::EventSource> teardownSource;
  uint32_t lastKeyTimeMs = 0;

  static void handleGlobal(void *data, wl_registry *registry, uint32_t name,
                           const char *interface, uint32_t /*version*/) {
    auto &self = *static_cast<Impl *>(data);
    const std::string_view kind(interface);
    if (!self.seat && kind == wl_seat_interface.name) {
      self.seat = static_cast<wl_seat *>(
          wl_registry_bind(registry, name, &wl_seat_interface, 1));
    } else if (!self.manager &&
               kind == zwp_virtual_keyboard_manager_v1_interface.name) {
      self.manager =
          static_cast<zwp_virtual_keyboard_manager_v1 *>(wl_registry_bind(
              registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1));
    }
  }

  static void handleGlobalRemove(void *, wl_registry *, uint32_t) {}

  static constexpr wl_registry_listener RegistryListener = {handleGlobal,
                                                            handleGlobalRemove};

  // Registry đã liệt kê xong mọi global khi callback sync đầu tiên quay về.
  static void handleProbeDone(void *data, wl_callback *callback, uint32_t) {
    auto &self = *static_cast<Impl *>(data);
    wl_callback_destroy(callback);
    self.probeCallback = nullptr;
    if (!self.seat || !self.manager) {
      // KWin và Mutter không cấp giao thức này cho client thường.
      self.fail("compositor has no zwp_virtual_keyboard_v1");
      return;
    }
    self.keyboard = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(
        self.manager, self.seat);
    self.state = State::Ready;
    self.log("ready");
  }

  static constexpr wl_callback_listener ProbeListener = {handleProbeDone};

  static void handleSyncDone(void *data, wl_callback *callback, uint32_t) {
    auto &self = *static_cast<Impl *>(data);
    wl_callback_destroy(callback);
    if (self.syncs.empty() || self.syncs.front().callback != callback) {
      return;
    }
    auto done = std::move(self.syncs.front().done);
    self.syncs.pop_front();
    if (done) {
      done(true);
    }
  }

  static constexpr wl_callback_listener SyncListener = {handleSyncDone};

  bool start() {
    if (state != State::Idle) {
      return state != State::Failed;
    }
    const char *waylandDisplay = std::getenv("WAYLAND_DISPLAY");
    if (!waylandDisplay || !*waylandDisplay) {
      state = State::Failed;
      log("unavailable reason=no-wayland-display");
      return false;
    }
    display = wl_display_connect(nullptr);
    if (!display) {
      fail("cannot connect to Wayland display");
      return false;
    }
    registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &RegistryListener, this);
    probeCallback = wl_display_sync(display);
    wl_callback_add_listener(probeCallback, &ProbeListener, this);
    displaySource = eventLoop.addIOEvent(
        wl_display_get_fd(display),
        fcitx::IOEventFlags{fcitx::IOEventFlag::In, fcitx::IOEventFlag::Hup,
                            fcitx::IOEventFlag::Err},
        [this](fcitx::EventSourceIO *, int, fcitx::IOEventFlags flags) {
          return handleDisplay(flags);
        });
    if (!displaySource) {
      fail("cannot watch Wayland display");
      return false;
    }
    state = State::Pending;
    return flush();
  }

  bool handleDisplay(fcitx::IOEventFlags flags) {
    if (flags.test(fcitx::IOEventFlag::Hup) ||
        flags.test(fcitx::IOEventFlag::Err) ||
        wl_display_dispatch(display) < 0) {
      fail("Wayland connection closed");
    }
    return state != State::Failed;
  }

  bool flush() {
    if (wl_display_flush(display) < 0 && errno != EAGAIN) {
      fail("cannot write to Wayland display");
      return false;
    }
    return true;
  }

  // Có thể được gọi khi libwayland đang dispatch, nên chỉ đổi state ở đây; kết
  // nối được đóng ở lượt event loop sau.
  void fail(const char *reason) {
    if (state == State::Failed) {
      return;
    }
    state = State::Failed;
    if (debugProvider()) {
      FCITX_WARN() << "areca: virtual-keyboard-device unavailable reason="
                   << (reason ? reason : "unknown");
    }
    teardownSource = eventLoop.addDeferEvent([this](fcitx::EventSource *) {
      auto source = std::move(teardownSource);
      displaySource.reset();
      auto pending = std::move(syncs);
      syncs.clear();
      for (auto &entry : pending) {
        wl_callback_destroy(entry.callback);
      }
      disconnect();
      for (auto &entry : pending) {
        if (entry.done) {
          entry.done(false);
        }
      }
      return false;
    });
    if (teardownSource) {
      teardownSource->setOneShot();
    }
  }

  void disconnect() {
    for (auto &entry : syncs) {
      wl_callback_destroy(entry.callback);
    }
    syncs.clear();
    if (probeCallback) {
      wl_callback_destroy(probeCallback);
      probeCallback = nullptr;
    }
    if (keyboard) {
      zwp_virtual_keyboard_v1_destroy(keyboard);
      keyboard = nullptr;
    }
    if (manager) {
      zwp_virtual_keyboard_manager_v1_destroy(manager);
      manager = nullptr;
    }
    if (seat) {
      wl_seat_destroy(seat);
      seat = nullptr;
    }
    if (registry) {
      wl_registry_destroy(registry);
      registry = nullptr;
    }
    if (display) {
      wl_display_disconnect(display);
      display = nullptr;
    }
  }

  // Keymap đi qua memfd, kết thúc bằng NUL như giao thức yêu cầu. Compositor
  // tự giữ bản sao nên fd được đóng ngay sau khi gửi.
  bool setKeymap(const std::string &keymap) {
    const int fd = memfd_create("areca-keymap", MFD_CLOEXEC);
    if (fd < 0) {
      FCITX_ERROR()
          << "areca: virtual-keyboard-device memfd_create failed errno="
          << errno;
      return false;
    }
    const size_t size = keymap.size() + 1;
    void *data = MAP_FAILED;
    if (ftruncate(fd, static_cast<off_t>(size)) == 0) {
      data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    }
    const bool mapped = data != MAP_FAILED;
    if (mapped) {
      std::memcpy(data, keymap.c_str(), size);
      munmap(data, size);
      zwp_virtual_keyboard_v1_keymap(keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                                     fd, static_cast<uint32_t>(size));
      zwp_virtual_keyboard_v1_modifiers(keyboard, 0, 0, 0, 0);
    }
    close(fd);
    return mapped;
  }

  // Thời điểm phím theo đồng hồ monotonic, tăng ngặt để hai phím liền nhau
  // không bị coi là cùng một lúc.
  uint32_t nextKeyTimeMs() {
    const auto nowMs =
        static_cast<uint32_t>(fcitx::now(CLOCK_MONOTONIC) / 1000);
    lastKeyTimeMs = nowMs > lastKeyTimeMs ? nowMs : lastKeyTimeMs + 1;
    return lastKeyTimeMs;
  }
#endif
};

VirtualKeyboardDevice::VirtualKeyboardDevice(fcitx::EventLoop &eventLoop,
                                             DebugProvider debugProvider)
    : impl_(std::make_unique<Impl>(eventLoop, std::move(debugProvider))) {}

VirtualKeyboardDevice::~VirtualKeyboardDevice() = default;

bool VirtualKeyboardDevice::warmUp() {
#ifdef ARECA_HAVE_WAYLAND_VK
  return impl_->start();
#else
  return false;
#endif
}

bool VirtualKeyboardDevice::isAvailable() {
#ifdef ARECA_HAVE_WAYLAND_VK
  if (impl_->state == Impl::State::Idle) {
    impl_->start();
  }
  return impl_->state == Impl::State::Ready;
#else
  return false;
#endif
}

bool VirtualKeyboardDevice::setKeymap(const std::string &keymap) {
#ifdef ARECA_HAVE_WAYLAND_VK
  return impl_->state == Impl::State::Ready && impl_->setKeymap(keymap);
#else
  (void)keymap;
  return false;
#endif
}

bool VirtualKeyboardDevice::sendKey(uint32_t evdevKeycode, bool press) {
#ifdef ARECA_HAVE_WAYLAND_VK
  if (impl_->state != Impl::State::Ready) {
    return false;
  }
  zwp_virtual_keyboard_v1_key(
      impl_->keyboard, impl_->nextKeyTimeMs(), evdevKeycode,
      press ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
  return true;
#else
  (void)evdevKeycode;
  (void)press;
  return false;
#endif
}

bool VirtualKeyboardDevice::sync(SyncCallback callback) {
#ifdef ARECA_HAVE_WAYLAND_VK
  if (impl_->state != Impl::State::Ready) {
    return false;
  }
  auto *syncCallback = wl_display_sync(impl_->display);
  wl_callback_add_listener(syncCallback, &Impl::SyncListener, impl_.get());
  impl_->syncs.push_back({syncCallback, std::move(callback)});
  return impl_->flush();
#else
  (void)callback;
  return false;
#endif
}

} // namespace areca
