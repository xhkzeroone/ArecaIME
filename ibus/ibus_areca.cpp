#include <ibus.h>
#include <linux/input.h>
#include <linux/uinput.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <sys/ioctl.h>
#include <unistd.h>

#include "bamboo_engine_adapter.h"
#include "adaptive_wait.h"
#include "program_compatibility.h"
#include "window_focus_tracker.h"

#ifndef ARECA_IBUS_CONFIG_PATH
#define ARECA_IBUS_CONFIG_PATH "/usr/share/ibus-areca/config.ini"
#endif

namespace {

constexpr const char *kEngineName = "areca";
constexpr const char *kBusName = "org.freedesktop.IBus.Areca";
constexpr guint kReleaseMask = 1U << 30U;
constexpr guint kModifierMask = IBUS_CONTROL_MASK | IBUS_MOD1_MASK |
                                IBUS_SUPER_MASK | IBUS_META_MASK;
constexpr guint kForwardFirstBarrierMs = 20;

struct Timing {
  guint backspaceDelayMs = 1;
  guint backspaceAckTimeoutMs = 150;
  guint afterBackspaceMs = 5;
  guint shiftSelectDelayMs = 1;
  guint shiftSelectAckTimeoutMs = 150;
  guint afterShiftSelectMs = 5;
  guint surroundingProbeTimeoutMs = 80;
  guint afterSurroundingMs = 1;
};

struct Config {
  std::string inputMethod = "Telex 2";
  bool spellCheck = true;
  bool modernStyle = true;
  bool debug = false;
  Timing timing;
};

guint readUnsigned(GKeyFile *file, const char *key, guint fallback) {
  GError *error = nullptr;
  const gint value = g_key_file_get_integer(file, "Timing", key, &error);
  if (error) {
    g_error_free(error);
    return fallback;
  }
  return value < 0 ? fallback : static_cast<guint>(value);
}

bool readBoolean(GKeyFile *file, const char *key, bool fallback) {
  GError *error = nullptr;
  const gboolean value =
      g_key_file_get_boolean(file, "Engine", key, &error);
  if (error) {
    g_error_free(error);
    return fallback;
  }
  return value != false;
}

Config loadConfig() {
  Config config;
  std::unique_ptr<GKeyFile, decltype(&g_key_file_unref)> file(
      g_key_file_new(), g_key_file_unref);
  const std::string userPath =
      std::string(g_get_user_config_dir()) + "/ibus-areca/config.ini";
  const char *path = g_file_test(userPath.c_str(), G_FILE_TEST_IS_REGULAR)
                         ? userPath.c_str()
                         : ARECA_IBUS_CONFIG_PATH;
  GError *error = nullptr;
  if (!g_key_file_load_from_file(file.get(), path, G_KEY_FILE_NONE,
                                 &error)) {
    if (error) {
      g_error_free(error);
    }
    return config;
  }

  if (gchar *value =
          g_key_file_get_string(file.get(), "Engine", "InputMethod", nullptr)) {
    config.inputMethod = value;
    g_free(value);
  }
  config.spellCheck = readBoolean(file.get(), "SpellCheck", config.spellCheck);
  config.modernStyle =
      readBoolean(file.get(), "ModernStyle", config.modernStyle);
  config.debug = readBoolean(file.get(), "Debug", config.debug);
  config.timing.backspaceDelayMs = readUnsigned(
      file.get(), "BackspaceDelayMs", config.timing.backspaceDelayMs);
  config.timing.backspaceAckTimeoutMs =
      readUnsigned(file.get(), "BackspaceAckTimeoutMs",
                   config.timing.backspaceAckTimeoutMs);
  config.timing.afterBackspaceMs = readUnsigned(
      file.get(), "AfterBackspaceMs", config.timing.afterBackspaceMs);
  config.timing.shiftSelectDelayMs = readUnsigned(
      file.get(), "ShiftSelectDelayMs", config.timing.shiftSelectDelayMs);
  config.timing.shiftSelectAckTimeoutMs =
      readUnsigned(file.get(), "ShiftSelectAckTimeoutMs",
                   config.timing.shiftSelectAckTimeoutMs);
  config.timing.afterShiftSelectMs = readUnsigned(
      file.get(), "AfterShiftSelectMs", config.timing.afterShiftSelectMs);
  config.timing.surroundingProbeTimeoutMs =
      readUnsigned(file.get(), "SurroundingProbeTimeoutMs",
                   config.timing.surroundingProbeTimeoutMs);
  config.timing.afterSurroundingMs = readUnsigned(
      file.get(), "AfterSurroundingMs", config.timing.afterSurroundingMs);
  return config;
}

class UinputDevice {
public:
  ~UinputDevice() {
    if (fd_ >= 0) {
      ioctl(fd_, UI_DEV_DESTROY);
      close(fd_);
    }
  }

  bool ensure() {
    if (initialized_) {
      return fd_ >= 0;
    }
    initialized_ = true;
    fd_ = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd_ < 0) {
      fd_ = open("/dev/input/uinput", O_WRONLY | O_NONBLOCK);
    }
    if (fd_ < 0) {
      return false;
    }
    if (ioctl(fd_, UI_SET_EVBIT, EV_KEY) < 0 ||
        ioctl(fd_, UI_SET_KEYBIT, KEY_LEFTSHIFT) < 0 ||
        ioctl(fd_, UI_SET_KEYBIT, KEY_LEFT) < 0 ||
        ioctl(fd_, UI_SET_KEYBIT, KEY_BACKSPACE) < 0 ||
        ioctl(fd_, UI_SET_EVBIT, EV_SYN) < 0) {
      close(fd_);
      fd_ = -1;
      return false;
    }

    uinput_setup setup{};
    setup.id.bustype = BUS_USB;
    setup.id.vendor = 0x1234;
    setup.id.product = 0x5678;
    std::strncpy(setup.name, "ArecaIME IBus Virtual Keyboard",
                 UINPUT_MAX_NAME_SIZE - 1);
    if (ioctl(fd_, UI_DEV_SETUP, &setup) < 0 ||
        ioctl(fd_, UI_DEV_CREATE) < 0) {
      close(fd_);
      fd_ = -1;
      return false;
    }
    return true;
  }

  void send(guint16 code, gint value) {
    if (fd_ < 0) {
      return;
    }
    input_event event{};
    event.type = EV_KEY;
    event.code = code;
    event.value = value;
    if (write(fd_, &event, sizeof(event)) < 0) {
      return;
    }
    event = {};
    event.type = EV_SYN;
    event.code = SYN_REPORT;
    (void)write(fd_, &event, sizeof(event));
  }

private:
  gint fd_ = -1;
  bool initialized_ = false;
};

enum class Backend { None, Surrounding, Backspace, ShiftSelect };

struct QueuedKey {
  guint keyval = 0;
  guint keycode = 0;
  guint state = 0;
};

class EngineState {
public:
  EngineState(IBusEngine *engine, Config config,
              areca::WindowFocusTracker *focusTracker)
      : engine_(engine), config_(std::move(config)),
        focusTracker_(focusTracker),
        bamboo_(config_.inputMethod, config_.spellCheck, true,
                config_.modernStyle) {}

  ~EngineState() { cancelTransaction(); }

  gboolean processKey(guint keyval, guint keycode, guint state) {
    const bool release = (state & kReleaseMask) != 0;
    if (forwardFirstBarrier_) {
      // Release của chính phím đầu phải đi cùng press đã forward. Mọi event
      // khác chờ editor nhận ký tự đầu trước khi bắt đầu rewrite.
      if (release && keyval == forwardedKeyval_ &&
          keycode == forwardedKeycode_) {
        return false;
      }
      queue_.push_back({keyval, keycode, state});
      return true;
    }
    if (pending_) {
      if (handleBackendEvent(keyval, release)) {
        return false;
      }
      // Giữ cả press và release để ứng dụng không nhận một release lẻ trong
      // lúc transaction rewrite đang chạy.
      queue_.push_back({keyval, keycode, state});
      return true;
    }
    if (release) {
      return false;
    }
    return processKeyNow(keyval, keycode, state);
  }

  void focusOut() { reset(); }

  void reset() {
    cancelTransaction();
    clearForwardFirstBarrier();
    queue_.clear();
    bamboo_.reset();
    surroundingValid_ = false;
  }

  void enable() {
    // This call also tells the client that the engine needs surrounding text.
    ibus_engine_get_surrounding_text(engine_, nullptr, nullptr, nullptr);
  }

  void setCapabilities(guint capabilities) { capabilities_ = capabilities; }

  void setContentType(guint purpose, guint hints) {
    purpose_ = purpose;
    hints_ = hints;
    if (privateInput()) {
      reset();
    }
  }

  void setCursorLocation(gint x, gint y, gint width, gint height) {
    cursorX_ = x;
    cursorY_ = y;
    cursorWidth_ = width;
    cursorHeight_ = height;
  }

  void setSurrounding(IBusText *text, guint cursor, guint anchor) {
    surroundingText_ = text && text->text ? text->text : "";
    surroundingCursor_ = cursor;
    surroundingAnchor_ = anchor;
    surroundingValid_ = true;

    if (pending_ && backend_ == Backend::Surrounding &&
        surroundingText_ == expectedSurroundingText_ &&
        surroundingCursor_ == expectedSurroundingCursor_) {
      clearTimer(waitTimer_);
      scheduleCommit(config_.timing.afterSurroundingMs);
    }
  }

private:
  static gboolean sourceThunk(gpointer data) {
    auto *callback = static_cast<std::function<void()> *>(data);
    (*callback)();
    return G_SOURCE_REMOVE;
  }

  static void destroyCallback(gpointer data) {
    delete static_cast<std::function<void()> *>(data);
  }

  guint schedule(guint delayMs, std::function<void()> callback) {
    auto *owned = new std::function<void()>(std::move(callback));
    if (delayMs == 0) {
      return g_idle_add_full(G_PRIORITY_DEFAULT, sourceThunk, owned,
                             destroyCallback);
    }
    return g_timeout_add_full(G_PRIORITY_DEFAULT, delayMs, sourceThunk, owned,
                              destroyCallback);
  }

  static void clearTimer(guint &source) {
    if (source != 0) {
      g_source_remove(source);
      source = 0;
    }
  }

  bool privateInput() const {
    return purpose_ == IBUS_INPUT_PURPOSE_PASSWORD ||
           purpose_ == IBUS_INPUT_PURPOSE_PIN ||
           (hints_ & IBUS_INPUT_HINT_HIDDEN_TEXT) != 0 ||
           (hints_ & IBUS_INPUT_HINT_PRIVATE) != 0;
  }

  bool inChromiumAddressBar() {
    if (purpose_ == IBUS_INPUT_PURPOSE_URL) {
      return true;
    }
    if (!focusTracker_ || !focusTracker_->isValid() ||
        !areca::isChromiumBrowser(focusTracker_->focusProgram())) {
      addressBarVerdictUsec_ = 0;
      return false;
    }

    const gint64 now = g_get_monotonic_time();
    if (focusTracker_->isBrowserUIFocused()) {
      addressBarVerdictUsec_ = now;
      return true;
    }
    if (addressBarVerdictUsec_ != 0 &&
        now - addressBarVerdictUsec_ <= 5 * G_USEC_PER_SEC) {
      return true;
    }
    addressBarVerdictUsec_ = 0;

    return cursorWidth_ <= 2 && cursorHeight_ >= 18 &&
           cursorHeight_ <= 24 && cursorY_ >= 0 && cursorY_ < 200;
  }

  gboolean processKeyNow(guint keyval, guint keycode, guint state) {
    if (privateInput()) {
      return false;
    }
    if ((state & kModifierMask) != 0) {
      bamboo_.reset();
      return false;
    }
    if (keyval == IBUS_KEY_BackSpace) {
      if (bamboo_.currentText().empty() && !bamboo_.canUndoMacro()) {
        bamboo_.reset();
        return false;
      }
      applyResult(bamboo_.processBackspace());
      return true;
    }
    if (keyval >= 0xff00) {
      bamboo_.reset();
      return false;
    }
    const gunichar codepoint = static_cast<gunichar>(keyval);
    if (!g_unichar_validate(codepoint) || !g_unichar_isprint(codepoint)) {
      bamboo_.reset();
      return false;
    }
    gchar utf8[7]{};
    const gint length = g_unichar_to_utf8(codepoint, utf8);
    const std::string text(utf8, static_cast<size_t>(length));
    if (bamboo_.currentText().empty() && !bamboo_.canProcessKey(codepoint)) {
      return false;
    }
    const bool firstCharacter = bamboo_.currentText().empty();
    const auto result = bamboo_.process(codepoint, text);
    if (firstCharacter && result.deleteCount == 0 &&
        result.commitText == text && !result.macroExpanded) {
      beginForwardFirstBarrier(keyval, keycode);
      return false;
    }
    applyResult(result);
    return true;
  }

  void beginForwardFirstBarrier(guint keyval, guint keycode) {
    clearForwardFirstBarrier();
    forwardFirstBarrier_ = true;
    forwardedKeyval_ = keyval;
    forwardedKeycode_ = keycode;
    forwardBarrierTimer_ = schedule(kForwardFirstBarrierMs, [this]() {
      forwardBarrierTimer_ = 0;
      forwardFirstBarrier_ = false;
      forwardedKeyval_ = 0;
      forwardedKeycode_ = 0;
      drainQueue();
    });
  }

  void clearForwardFirstBarrier() {
    clearTimer(forwardBarrierTimer_);
    forwardFirstBarrier_ = false;
    forwardedKeyval_ = 0;
    forwardedKeycode_ = 0;
  }

  void applyResult(const areca::BambooResult &result) {
    if (result.deleteCount == 0) {
      commit(result.commitText);
      drainQueue();
      return;
    }

    pending_ = true;
    adaptiveWait_.beginTransaction();
    deleteCount_ = result.deleteCount;
    commitText_ = result.commitText;
    seenEvents_ = 0;

    const bool url = inChromiumAddressBar();
    const bool hasSurrounding =
        surroundingValid_ &&
        (capabilities_ & IBUS_CAP_SURROUNDING_TEXT) != 0;
    if (!url && hasSurrounding && beginSurrounding()) {
      return;
    }
    if (url && beginShiftSelect()) {
      return;
    }
    if (beginBackspace()) {
      return;
    }
    // No rewrite backend is available. Reset Bamboo so the next key starts
    // from the text that is still visible in the client.
    cancelTransaction();
    bamboo_.reset();
    drainQueue();
  }

  bool beginSurrounding() {
    if (!g_utf8_validate(surroundingText_.c_str(), -1, nullptr)) {
      return false;
    }
    const auto characterCount = static_cast<guint>(
        g_utf8_strlen(surroundingText_.c_str(), -1));
    if (surroundingCursor_ > characterCount ||
        surroundingCursor_ < deleteCount_) {
      return false;
    }
    const gchar *begin = surroundingText_.c_str();
    const gchar *eraseBegin =
        g_utf8_offset_to_pointer(begin, surroundingCursor_ - deleteCount_);
    const gchar *eraseEnd = g_utf8_offset_to_pointer(begin, surroundingCursor_);
    expectedSurroundingText_.assign(begin, eraseBegin);
    expectedSurroundingText_.append(eraseEnd);
    expectedSurroundingCursor_ = surroundingCursor_ - deleteCount_;
    backend_ = Backend::Surrounding;
    ibus_engine_delete_surrounding_text(engine_,
                                        -static_cast<gint>(deleteCount_),
                                        deleteCount_);
    waitTimer_ = schedule(config_.timing.surroundingProbeTimeoutMs, [this]() {
      waitTimer_ = 0;
      if (!pending_ || backend_ != Backend::Surrounding) {
        return;
      }
      if (beginBackspace()) {
        return;
      }
      cancelTransaction();
      bamboo_.reset();
      drainQueue();
    });
    return true;
  }

  bool beginBackspace() {
    if (!uinput_.ensure()) {
      return false;
    }
    clearTimer(sendTimer_);
    clearTimer(waitTimer_);
    backend_ = Backend::Backspace;
    remainingEvents_ = deleteCount_;
    sendNextBackspace();
    return true;
  }

  void sendNextBackspace() {
    if (!pending_ || backend_ != Backend::Backspace) {
      return;
    }
    uinput_.send(KEY_BACKSPACE, 1);
    uinput_.send(KEY_BACKSPACE, 0);
    if (--remainingEvents_ > 0) {
      sendTimer_ = schedule(
          adaptiveWait_.effectiveBackspaceDelayMs(
              config_.timing.backspaceDelayMs),
          [this]() {
        sendTimer_ = 0;
        sendNextBackspace();
      });
      return;
    }
    ackStartedUsec_ = g_get_monotonic_time();
    waitTimer_ = schedule(config_.timing.backspaceAckTimeoutMs, [this]() {
      waitTimer_ = 0;
      adaptiveWait_.observeAckTimeout();
      scheduleCommit(config_.timing.afterBackspaceMs);
    });
  }

  bool beginShiftSelect() {
    if (!uinput_.ensure()) {
      return false;
    }
    clearTimer(sendTimer_);
    clearTimer(waitTimer_);
    backend_ = Backend::ShiftSelect;
    remainingEvents_ = deleteCount_;
    shiftHeld_ = true;
    uinput_.send(KEY_LEFTSHIFT, 1);
    sendTimer_ = schedule(config_.timing.shiftSelectDelayMs, [this]() {
      sendTimer_ = 0;
      sendNextLeft();
    });
    return true;
  }

  void sendNextLeft() {
    if (!pending_ || backend_ != Backend::ShiftSelect) {
      return;
    }
    uinput_.send(KEY_LEFT, 1);
    uinput_.send(KEY_LEFT, 0);
    if (--remainingEvents_ > 0) {
      sendTimer_ = schedule(config_.timing.shiftSelectDelayMs, [this]() {
        sendTimer_ = 0;
        sendNextLeft();
      });
      return;
    }
    sendTimer_ = schedule(config_.timing.shiftSelectDelayMs, [this]() {
      sendTimer_ = 0;
      shiftHeld_ = false;
      uinput_.send(KEY_LEFTSHIFT, 0);
      ackStartedUsec_ = g_get_monotonic_time();
      waitTimer_ = schedule(config_.timing.shiftSelectAckTimeoutMs, [this]() {
        waitTimer_ = 0;
        adaptiveWait_.observeAckTimeout();
        scheduleCommit(config_.timing.afterShiftSelectMs);
      });
    });
  }

  bool handleBackendEvent(guint keyval, bool release) {
    if (backend_ == Backend::Backspace && keyval == IBUS_KEY_BackSpace) {
      if (!release && ++seenEvents_ == deleteCount_) {
        clearTimer(waitTimer_);
        observeAckRoundtrip();
        scheduleCommit(config_.timing.afterBackspaceMs);
      }
      return true;
    }
    if (backend_ == Backend::ShiftSelect) {
      const bool shift = keyval == IBUS_KEY_Shift_L;
      const bool left = keyval == IBUS_KEY_Left;
      if (shift && release && !shiftHeld_) {
        clearTimer(waitTimer_);
        observeAckRoundtrip();
        scheduleCommit(config_.timing.afterShiftSelectMs);
      }
      return shift || left;
    }
    return false;
  }

  void scheduleCommit(guint delayMs) {
    clearTimer(waitTimer_);
    waitTimer_ = schedule(adaptiveWait_.effectiveWaitMs(delayMs), [this]() {
      waitTimer_ = 0;
      if (!pending_) {
        return;
      }
      commit(commitText_);
      finishTransaction();
    });
  }

  void commit(const std::string &text) {
    if (!text.empty()) {
      ibus_engine_commit_text(engine_, ibus_text_new_from_string(text.c_str()));
    }
  }

  void finishTransaction() {
    clearTimer(sendTimer_);
    clearTimer(waitTimer_);
    pending_ = false;
    backend_ = Backend::None;
    deleteCount_ = 0;
    remainingEvents_ = 0;
    seenEvents_ = 0;
    shiftHeld_ = false;
    commitText_.clear();
    expectedSurroundingText_.clear();
    ackStartedUsec_ = 0;
    adaptiveWait_.completeTransaction();
    drainQueue();
  }

  void cancelTransaction() {
    clearTimer(sendTimer_);
    clearTimer(waitTimer_);
    if (shiftHeld_) {
      uinput_.send(KEY_LEFTSHIFT, 0);
    }
    pending_ = false;
    backend_ = Backend::None;
    shiftHeld_ = false;
    commitText_.clear();
    ackStartedUsec_ = 0;
    adaptiveWait_.cancelTransaction();
  }

  void observeAckRoundtrip() {
    if (ackStartedUsec_ == 0) {
      return;
    }
    const gint64 now = g_get_monotonic_time();
    if (now > ackStartedUsec_) {
      adaptiveWait_.observeAckRoundtrip(
          static_cast<uint64_t>(now - ackStartedUsec_));
    }
    ackStartedUsec_ = 0;
  }

  void drainQueue() {
    while (!pending_ && !forwardFirstBarrier_ && !queue_.empty()) {
      const auto key = queue_.front();
      queue_.pop_front();
      const bool release = (key.state & kReleaseMask) != 0;
      if (release || !processKeyNow(key.keyval, key.keycode, key.state)) {
        ibus_engine_forward_key_event(engine_, key.keyval, key.keycode,
                                      key.state);
      }
    }
  }

  IBusEngine *engine_ = nullptr;
  Config config_;
  areca::WindowFocusTracker *focusTracker_ = nullptr;
  areca::BambooEngineAdapter bamboo_;
  UinputDevice uinput_;
  areca::AdaptiveWait adaptiveWait_;
  std::deque<QueuedKey> queue_;
  guint capabilities_ = 0;
  guint purpose_ = IBUS_INPUT_PURPOSE_FREE_FORM;
  guint hints_ = IBUS_INPUT_HINT_NONE;
  gint cursorX_ = 0;
  gint cursorY_ = 0;
  gint cursorWidth_ = 0;
  gint cursorHeight_ = 0;
  gint64 addressBarVerdictUsec_ = 0;
  std::string surroundingText_;
  guint surroundingCursor_ = 0;
  guint surroundingAnchor_ = 0;
  bool surroundingValid_ = false;
  bool pending_ = false;
  Backend backend_ = Backend::None;
  guint deleteCount_ = 0;
  guint remainingEvents_ = 0;
  guint seenEvents_ = 0;
  bool shiftHeld_ = false;
  std::string commitText_;
  std::string expectedSurroundingText_;
  guint expectedSurroundingCursor_ = 0;
  guint sendTimer_ = 0;
  guint waitTimer_ = 0;
  gint64 ackStartedUsec_ = 0;
  bool forwardFirstBarrier_ = false;
  guint forwardedKeyval_ = 0;
  guint forwardedKeycode_ = 0;
  guint forwardBarrierTimer_ = 0;
};

EngineState *stateFor(IBusEngine *engine) {
  return static_cast<EngineState *>(
      g_object_get_data(G_OBJECT(engine), "ibus-areca-state"));
}

gboolean processKey(IBusEngine *engine, guint keyval, guint keycode,
                    guint state, gpointer) {
  return stateFor(engine)->processKey(keyval, keycode, state);
}

void focusOut(IBusEngine *engine, gpointer) { stateFor(engine)->focusOut(); }
void reset(IBusEngine *engine, gpointer) { stateFor(engine)->reset(); }
void enable(IBusEngine *engine, gpointer) { stateFor(engine)->enable(); }
void setCapabilities(IBusEngine *engine, guint capabilities, gpointer) {
  stateFor(engine)->setCapabilities(capabilities);
}
void setContentType(IBusEngine *engine, guint purpose, guint hints, gpointer) {
  stateFor(engine)->setContentType(purpose, hints);
}
void setCursorLocation(IBusEngine *engine, gint x, gint y, gint width,
                       gint height, gpointer) {
  stateFor(engine)->setCursorLocation(x, y, width, height);
}
void setSurrounding(IBusEngine *engine, IBusText *text, guint cursor,
                    guint anchor, gpointer) {
  stateFor(engine)->setSurrounding(text, cursor, anchor);
}

GDBusConnection *gConnection = nullptr;
guint64 engineSerial = 0;
std::unique_ptr<areca::WindowFocusTracker> gFocusTracker;

IBusEngine *createEngine(IBusFactory *, const gchar *engineName, gpointer) {
  const std::string path = "/org/freedesktop/IBus/Engine/Areca/" +
                           std::to_string(++engineSerial);
  IBusEngine *engine =
      ibus_engine_new(engineName, path.c_str(), gConnection);
  auto *state =
      new EngineState(engine, loadConfig(), gFocusTracker.get());
  g_object_set_data_full(G_OBJECT(engine), "ibus-areca-state", state,
                         [](gpointer data) {
                           delete static_cast<EngineState *>(data);
                         });
  g_signal_connect(engine, "process-key-event", G_CALLBACK(processKey),
                   nullptr);
  g_signal_connect(engine, "focus-out", G_CALLBACK(focusOut), nullptr);
  g_signal_connect(engine, "reset", G_CALLBACK(reset), nullptr);
  g_signal_connect(engine, "enable", G_CALLBACK(enable), nullptr);
  g_signal_connect(engine, "set-capabilities", G_CALLBACK(setCapabilities),
                   nullptr);
  g_signal_connect(engine, "set-content-type", G_CALLBACK(setContentType),
                   nullptr);
  g_signal_connect(engine, "set-cursor-location",
                   G_CALLBACK(setCursorLocation), nullptr);
  g_signal_connect(engine, "set-surrounding-text", G_CALLBACK(setSurrounding),
                   nullptr);
  return engine;
}

} // namespace

int main(int argc, char **argv) {
  if (argc > 1 && std::string_view(argv[1]) == "--self-test") {
    areca::BambooEngineAdapter engine;
    for (const char character : std::string_view("tieengs")) {
      const std::string text(1, character);
      (void)engine.process(static_cast<uint32_t>(character), text);
    }
    if (engine.currentText() != "tiếng") {
      g_printerr("ibus-areca: Bamboo self-test failed: %s\n",
                 engine.currentText().c_str());
      return 1;
    }
    g_print("ibus-areca: self-test passed\n");
    return 0;
  }
  ibus_init();
  gFocusTracker = std::make_unique<areca::WindowFocusTracker>();
  if (!gFocusTracker->start()) {
    gFocusTracker.reset();
  }
  IBusBus *bus = ibus_bus_new();
  if (!ibus_bus_is_connected(bus)) {
    g_printerr("ibus-areca: cannot connect to ibus-daemon\n");
    return 1;
  }
  gConnection = ibus_bus_get_connection(bus);
  IBusFactory *factory = ibus_factory_new(gConnection);
  g_signal_connect(factory, "create-engine", G_CALLBACK(createEngine), nullptr);
  ibus_bus_request_name(bus, kBusName, 0);

  if (argc > 1 && std::string_view(argv[1]) == "--verbose") {
    g_message("ibus-areca: ready as engine %s", kEngineName);
  }
  ibus_main();
  gFocusTracker.reset();
  g_object_unref(factory);
  g_object_unref(bus);
  return 0;
}
