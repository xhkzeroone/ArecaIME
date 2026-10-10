#include "native_device.h"

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

#include <fcitx-utils/log.h>

#ifdef ARECA_HAVE_LIBEI
#include <glib-unix.h>
#include <libei.h>
#include <libportal/portal.h>
#include <linux/input-event-codes.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#endif

namespace areca {
namespace {

bool isWaylandSession() {
  const char *session = std::getenv("XDG_SESSION_TYPE");
  const char *waylandDisplay = std::getenv("WAYLAND_DISPLAY");
  return (session && std::string(session) == "wayland") ||
         (waylandDisplay && *waylandDisplay);
}

} // namespace

struct NativeDevice::Impl {
  enum class State { Idle, Pending, Ready, Failed };

  Impl(fcitx::EventLoop &eventLoop, DebugProvider provider)
      : eventLoop(eventLoop), debugProvider(std::move(provider)),
        xtest(debugProvider), wayland(isWaylandSession()) {}

  ~Impl() {
#ifdef ARECA_HAVE_LIBEI
    stopPortalWorker();
#endif
    closeEi();
  }

  fcitx::EventLoop &eventLoop;
  DebugProvider debugProvider;
  XTestDevice xtest;
  const bool wayland;
  State state = State::Idle;

#ifdef ARECA_HAVE_LIBEI
  struct ei *connection = nullptr;
  struct ei_device *keyboard = nullptr;
  uint32_t sequence = 0;
  std::unique_ptr<fcitx::EventSourceIO> resultSource;
  std::unique_ptr<fcitx::EventSourceIO> eiSource;

  std::atomic<bool> stop{false};
  std::thread portalWorker;
  std::mutex resultMutex;
  int resultFd = -1;
  int workerWakeFd = -1;
  int pendingEiFd = -1;
  std::string pendingError;

  GMainContext *portalContext = nullptr;
  GCancellable *cancellable = nullptr;
  XdpPortal *portal = nullptr;
  XdpSession *session = nullptr;
  GSource *wakeSource = nullptr;
  bool pending = false;
#endif

  void log(const char *message) const {
    if (debugProvider()) {
      FCITX_INFO() << "areca: native-device " << message;
    }
  }

  void closeEi() {
#ifdef ARECA_HAVE_LIBEI
    eiSource.reset();
    if (keyboard) {
      keyboard = ei_device_unref(keyboard);
    }
    if (connection) {
      connection = ei_unref(connection);
    }
#endif
  }

  void failToXTest(const char *reason) {
    if (state == State::Failed) {
      return;
    }
    state = State::Failed;
    if (debugProvider()) {
      FCITX_WARN() << "areca: libei unavailable, falling back to XTest: "
                   << (reason ? reason : "unknown error");
    }
    // Request fallback permission as soon as the portal path fails, rather
    // than waiting for the next typed character.
    xtest.warmUp();
  }

#ifdef ARECA_HAVE_LIBEI
#ifdef ARECA_LIBPORTAL_PERSIST
  static std::filesystem::path tokenPath() {
    const char *xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
      return std::filesystem::path(xdg) / "fcitx5" /
             "areca-libei-restore-token";
    }
    const char *home = std::getenv("HOME");
    if (home && *home) {
      return std::filesystem::path(home) / ".config" / "fcitx5" /
             "areca-libei-restore-token";
    }
    return {};
  }

  static std::string loadToken() {
    const auto path = tokenPath();
    if (path.empty()) {
      return {};
    }
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
      return {};
    }
    std::string token;
    char buffer[512];
    for (;;) {
      const ssize_t count = read(fd, buffer, sizeof(buffer));
      if (count > 0) {
        token.append(buffer, static_cast<size_t>(count));
      } else if (count < 0 && errno == EINTR) {
        continue;
      } else {
        break;
      }
    }
    close(fd);
    const auto newline = token.find_first_of("\r\n");
    if (newline != std::string::npos) {
      token.resize(newline);
    }
    return token;
  }

  static void saveToken(XdpSession *portalSession) {
    char *value = xdp_session_get_restore_token(portalSession);
    if (!value || !*value) {
      g_free(value);
      return;
    }
    const auto path = tokenPath();
    if (path.empty()) {
      g_free(value);
      return;
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
      g_free(value);
      return;
    }
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
                        S_IRUSR | S_IWUSR);
    if (fd >= 0) {
      (void)fchmod(fd, S_IRUSR | S_IWUSR);
      const size_t length = std::char_traits<char>::length(value);
      size_t written = 0;
      while (written < length) {
        const ssize_t count = write(fd, value + written, length - written);
        if (count > 0) {
          written += static_cast<size_t>(count);
        } else if (count < 0 && errno == EINTR) {
          continue;
        } else {
          break;
        }
      }
      close(fd);
    }
    g_free(value);
  }
#endif

  void notifyMain() {
    const uint64_t one = 1;
    if (resultFd >= 0) {
      (void)write(resultFd, &one, sizeof(one));
    }
  }

  void publishEiFd(int fd) {
    {
      std::lock_guard<std::mutex> lock(resultMutex);
      if (pendingEiFd >= 0) {
        close(pendingEiFd);
      }
      pendingEiFd = fd;
      pendingError.clear();
    }
    notifyMain();
  }

  void publishError(const char *message) {
    {
      std::lock_guard<std::mutex> lock(resultMutex);
      pendingError = message && *message ? message : "portal session failed";
    }
    notifyMain();
  }

  static void sessionClosed(XdpSession *, gpointer data) {
    static_cast<Impl *>(data)->publishError("portal session closed");
  }

  static void sessionStarted(GObject *object, GAsyncResult *result,
                             gpointer data) {
    auto &self = *static_cast<Impl *>(data);
    self.pending = false;
    GError *error = nullptr;
    if (!xdp_session_start_finish(XDP_SESSION(object), result, &error)) {
      self.publishError(error ? error->message : "portal start failed");
      g_clear_error(&error);
      return;
    }
#ifdef ARECA_LIBPORTAL_PERSIST
    saveToken(self.session);
#endif
    if (!(xdp_session_get_devices(self.session) & XDP_DEVICE_KEYBOARD)) {
      self.publishError("portal did not grant keyboard access");
      return;
    }
    const int fd = xdp_session_connect_to_eis(self.session, &error);
    if (fd < 0) {
      self.publishError(error ? error->message
                              : "portal did not provide an EIS connection");
      g_clear_error(&error);
      return;
    }
    self.publishEiFd(fd);
  }

  static void sessionCreated(GObject *object, GAsyncResult *result,
                             gpointer data) {
    auto &self = *static_cast<Impl *>(data);
    self.pending = false;
    GError *error = nullptr;
    self.session = xdp_portal_create_remote_desktop_session_finish(
        XDP_PORTAL(object), result, &error);
    if (!self.session) {
      self.publishError(error ? error->message : "portal create failed");
      g_clear_error(&error);
      return;
    }
    g_signal_connect(self.session, "closed", G_CALLBACK(sessionClosed), &self);
    if (self.stop.load()) {
      return;
    }
    self.pending = true;
    xdp_session_start(self.session, nullptr, self.cancellable, sessionStarted,
                      &self);
  }

  static gboolean workerWake(gint fd, GIOCondition, gpointer data) {
    auto &self = *static_cast<Impl *>(data);
    uint64_t value = 0;
    while (read(fd, &value, sizeof(value)) < 0 && errno == EINTR) {
    }
    if (self.stop.load() && self.cancellable) {
      g_cancellable_cancel(self.cancellable);
    }
    return G_SOURCE_CONTINUE;
  }

  void runPortalWorker() {
    portalContext = g_main_context_new();
    g_main_context_push_thread_default(portalContext);
    cancellable = g_cancellable_new();
    wakeSource = g_unix_fd_source_new(workerWakeFd, G_IO_IN);
    g_source_set_callback(wakeSource, G_SOURCE_FUNC(workerWake), this, nullptr);
    g_source_attach(wakeSource, portalContext);
    portal = xdp_portal_new();
    if (!portal) {
      publishError("cannot create portal proxy");
    } else {
      pending = true;
#ifdef ARECA_LIBPORTAL_PERSIST
      const std::string restoreToken = loadToken();
      xdp_portal_create_remote_desktop_session_full(
          portal, XDP_DEVICE_KEYBOARD, static_cast<XdpOutputType>(0),
          XDP_REMOTE_DESKTOP_FLAG_NONE, XDP_CURSOR_MODE_HIDDEN,
          XDP_PERSIST_MODE_PERSISTENT,
          restoreToken.empty() ? nullptr : restoreToken.c_str(), cancellable,
          sessionCreated, this);
#else
      xdp_portal_create_remote_desktop_session(
          portal, XDP_DEVICE_KEYBOARD, static_cast<XdpOutputType>(0),
          XDP_REMOTE_DESKTOP_FLAG_NONE, XDP_CURSOR_MODE_HIDDEN, cancellable,
          sessionCreated, this);
#endif
      while (!stop.load() || pending) {
        g_main_context_iteration(portalContext, TRUE);
      }
    }

    if (wakeSource) {
      g_source_destroy(wakeSource);
      g_source_unref(wakeSource);
      wakeSource = nullptr;
    }
    if (session) {
      xdp_session_close(session);
      g_object_unref(session);
      session = nullptr;
    }
    if (portal) {
      g_object_unref(portal);
      portal = nullptr;
    }
    g_object_unref(cancellable);
    cancellable = nullptr;
    g_main_context_pop_thread_default(portalContext);
    g_main_context_unref(portalContext);
    portalContext = nullptr;
  }

  bool setupEi(int fd) {
    connection = ei_new_sender(this);
    if (!connection) {
      close(fd);
      failToXTest("ei_new_sender failed");
      return false;
    }
    ei_configure_name(connection, "Areca Vietnamese Input Method");
    if (ei_setup_backend_fd(connection, fd) < 0) {
      failToXTest("EI handshake setup failed");
      return false;
    }
    eiSource = eventLoop.addIOEvent(
        ei_get_fd(connection),
        fcitx::IOEventFlags{fcitx::IOEventFlag::In, fcitx::IOEventFlag::Hup,
                            fcitx::IOEventFlag::Err},
        [this](fcitx::EventSourceIO *, int, fcitx::IOEventFlags flags) {
          return handleEi(flags);
        });
    if (!eiSource) {
      failToXTest("cannot watch EIS file descriptor");
      return false;
    }
    return true;
  }

  bool handlePortalResult(fcitx::IOEventFlags flags) {
    if (flags.test(fcitx::IOEventFlag::Hup) ||
        flags.test(fcitx::IOEventFlag::Err)) {
      failToXTest("portal worker notification channel closed");
      return false;
    }
    uint64_t value = 0;
    while (read(resultFd, &value, sizeof(value)) < 0 && errno == EINTR) {
    }
    int fd = -1;
    std::string error;
    {
      std::lock_guard<std::mutex> lock(resultMutex);
      fd = std::exchange(pendingEiFd, -1);
      error = std::move(pendingError);
      pendingError.clear();
    }
    if (!error.empty()) {
      if (fd >= 0) {
        close(fd);
      }
      failToXTest(error.c_str());
      return true;
    }
    if (fd >= 0) {
      log("portal granted persistent keyboard access");
      setupEi(fd);
    }
    return true;
  }

  bool handleEi(fcitx::IOEventFlags flags) {
    if (flags.test(fcitx::IOEventFlag::Hup) ||
        flags.test(fcitx::IOEventFlag::Err)) {
      failToXTest("EIS connection closed");
      return false;
    }
    ei_dispatch(connection);
    while (auto *event = ei_get_event(connection)) {
      const auto type = ei_event_get_type(event);
      auto *device = ei_event_get_device(event);
      if (type == EI_EVENT_SEAT_ADDED) {
        ei_seat_bind_capabilities(ei_event_get_seat(event),
                                  EI_DEVICE_CAP_KEYBOARD, nullptr);
      } else if (type == EI_EVENT_DEVICE_RESUMED && !keyboard &&
                 ei_device_has_capability(device, EI_DEVICE_CAP_KEYBOARD)) {
        keyboard = ei_device_ref(device);
        ei_device_start_emulating(keyboard, ++sequence);
        state = State::Ready;
        log("libei keyboard ready");
      } else if ((type == EI_EVENT_DEVICE_PAUSED ||
                  type == EI_EVENT_DEVICE_REMOVED) &&
                 device == keyboard) {
        keyboard = ei_device_unref(keyboard);
        ei_event_unref(event);
        failToXTest("EIS keyboard paused or removed");
        return false;
      } else if (type == EI_EVENT_DISCONNECT) {
        ei_event_unref(event);
        failToXTest("EIS disconnected");
        return false;
      }
      ei_event_unref(event);
    }
    return true;
  }

  bool startPortalWorker() {
    if (state != State::Idle) {
      return state != State::Failed;
    }
    resultFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    workerWakeFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (resultFd < 0 || workerWakeFd < 0) {
      failToXTest("cannot create portal worker notification channel");
      return false;
    }
    resultSource = eventLoop.addIOEvent(
        resultFd,
        fcitx::IOEventFlags{fcitx::IOEventFlag::In, fcitx::IOEventFlag::Hup,
                            fcitx::IOEventFlag::Err},
        [this](fcitx::EventSourceIO *, int, fcitx::IOEventFlags flags) {
          return handlePortalResult(flags);
        });
    if (!resultSource) {
      failToXTest("cannot watch portal worker notification channel");
      return false;
    }
    state = State::Pending;
    portalWorker = std::thread([this]() { runPortalWorker(); });
    log("persistent keyboard permission requested");
    return true;
  }

  void stopPortalWorker() {
    stop.store(true);
    if (workerWakeFd >= 0) {
      const uint64_t one = 1;
      (void)write(workerWakeFd, &one, sizeof(one));
    }
    if (portalWorker.joinable()) {
      portalWorker.join();
    }
    resultSource.reset();
    {
      std::lock_guard<std::mutex> lock(resultMutex);
      if (pendingEiFd >= 0) {
        close(pendingEiFd);
        pendingEiFd = -1;
      }
    }
    if (resultFd >= 0) {
      close(resultFd);
      resultFd = -1;
    }
    if (workerWakeFd >= 0) {
      close(workerWakeFd);
      workerWakeFd = -1;
    }
  }
#endif
};

NativeDevice::NativeDevice(fcitx::EventLoop &eventLoop,
                           DebugProvider debugProvider)
    : impl_(std::make_unique<Impl>(eventLoop, std::move(debugProvider))) {}

NativeDevice::~NativeDevice() = default;

bool NativeDevice::warmUp() {
  if (!impl_->wayland) {
    return impl_->xtest.ensureDevice();
  }
#ifdef ARECA_HAVE_LIBEI
  return impl_->startPortalWorker();
#else
  return impl_->xtest.warmUp();
#endif
}

bool NativeDevice::isAvailable() {
  if (!impl_->wayland) {
    return impl_->xtest.isAvailable();
  }
#ifdef ARECA_HAVE_LIBEI
  if (impl_->state == Impl::State::Idle) {
    impl_->startPortalWorker();
  }
  if (impl_->state == Impl::State::Ready) {
    return true;
  }
  if (impl_->state == Impl::State::Failed) {
    return impl_->xtest.isAvailable();
  }
  return false;
#else
  return impl_->xtest.isAvailable();
#endif
}

bool NativeDevice::sendBackspace() {
  if (!impl_->wayland) {
    return impl_->xtest.sendBackspace();
  }
#ifdef ARECA_HAVE_LIBEI
  if (impl_->state == Impl::State::Ready && impl_->keyboard &&
      impl_->connection) {
    ei_device_keyboard_key(impl_->keyboard, KEY_BACKSPACE, true);
    ei_device_frame(impl_->keyboard, ei_now(impl_->connection));
    ei_device_keyboard_key(impl_->keyboard, KEY_BACKSPACE, false);
    ei_device_frame(impl_->keyboard, ei_now(impl_->connection));
    return true;
  }
  if (impl_->state == Impl::State::Failed) {
    return impl_->xtest.sendBackspace();
  }
  return false;
#else
  return impl_->xtest.sendBackspace();
#endif
}

bool NativeDevice::sendShift(bool press) {
  if (!impl_->wayland) {
    return impl_->xtest.sendShift(press);
  }
#ifdef ARECA_HAVE_LIBEI
  if (impl_->state == Impl::State::Ready && impl_->keyboard &&
      impl_->connection) {
    ei_device_keyboard_key(impl_->keyboard, KEY_LEFTSHIFT, press);
    ei_device_frame(impl_->keyboard, ei_now(impl_->connection));
    return true;
  }
  if (impl_->state == Impl::State::Failed) {
    return impl_->xtest.sendShift(press);
  }
  return false;
#else
  return impl_->xtest.sendShift(press);
#endif
}

bool NativeDevice::sendLeft() {
  if (!impl_->wayland) {
    return impl_->xtest.sendLeft();
  }
#ifdef ARECA_HAVE_LIBEI
  if (impl_->state == Impl::State::Ready && impl_->keyboard &&
      impl_->connection) {
    ei_device_keyboard_key(impl_->keyboard, KEY_LEFT, true);
    ei_device_frame(impl_->keyboard, ei_now(impl_->connection));
    ei_device_keyboard_key(impl_->keyboard, KEY_LEFT, false);
    ei_device_frame(impl_->keyboard, ei_now(impl_->connection));
    return true;
  }
  if (impl_->state == Impl::State::Failed) {
    return impl_->xtest.sendLeft();
  }
  return false;
#else
  return impl_->xtest.sendLeft();
#endif
}

} // namespace areca
