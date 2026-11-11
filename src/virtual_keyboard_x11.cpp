#include "virtual_keyboard_device.h"

#ifdef ARECA_HAVE_XCB_XTEST

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include <fcitx-utils/log.h>
#include <xcb/xcb.h>
#include <xcb/xtest.h>

#include "serial_worker.h"

namespace areca {
namespace {

// Keysym Unicode của X11: 0x01000000 + mã ký tự.
constexpr xcb_keysym_t kUnicodeKeysymBase = 0x01000000;
// Số keycode rảnh mượn tối đa cho một lượt gõ.
constexpr size_t kMaxBorrowedKeycodes = 48;
// Ứng dụng đọc keymap bất đồng bộ: giữ keycode mượn thêm một lúc sau phím cuối
// để phím đến muộn vẫn ra đúng ký tự.
constexpr auto kRestoreDelay = std::chrono::milliseconds(50);

struct ConnectionDeleter {
  void operator()(xcb_connection_t *connection) const noexcept {
    xcb_disconnect(connection);
  }
};
using ConnectionPtr = std::unique_ptr<xcb_connection_t, ConnectionDeleter>;

template <typename T> struct ReplyDeleter {
  void operator()(T *reply) const noexcept { std::free(reply); }
};
template <typename T> using ReplyPtr = std::unique_ptr<T, ReplyDeleter<T>>;

ConnectionPtr connect() {
  ConnectionPtr connection(xcb_connect(nullptr, nullptr));
  if (!connection || xcb_connection_has_error(connection.get()) != 0) {
    return {};
  }
  return connection;
}

bool hasXTest(xcb_connection_t *connection) {
  const auto *extension = xcb_get_extension_data(connection, &xcb_test_id);
  if (!extension || !extension->present) {
    return false;
  }
  ReplyPtr<xcb_test_get_version_reply_t> version(xcb_test_get_version_reply(
      connection, xcb_test_get_version(connection, 2, 2), nullptr));
  return version != nullptr;
}

// Bản sao keymap của X server để tìm keycode và trả lại keycode đã mượn.
struct KeyboardMap {
  xcb_keycode_t minKeycode = 0;
  xcb_keycode_t maxKeycode = 0;
  uint8_t perKeycode = 0;
  std::vector<xcb_keysym_t> keysyms;

  bool valid() const { return perKeycode > 0 && !keysyms.empty(); }

  std::vector<xcb_keysym_t> of(xcb_keycode_t keycode) const {
    const auto begin =
        keysyms.begin() +
        static_cast<std::ptrdiff_t>((keycode - minKeycode) * perKeycode);
    return {begin, begin + perKeycode};
  }

  std::optional<xcb_keycode_t> find(xcb_keysym_t keysym) const {
    for (unsigned keycode = minKeycode; keycode <= maxKeycode; ++keycode) {
      for (const auto value : of(static_cast<xcb_keycode_t>(keycode))) {
        if (value == keysym) {
          return static_cast<xcb_keycode_t>(keycode);
        }
      }
    }
    return std::nullopt;
  }

  // Các keycode không gán keysym nào: mượn được mà không đổi phím thật.
  std::vector<xcb_keycode_t> spares(size_t limit) const {
    std::vector<xcb_keycode_t> result;
    for (unsigned keycode = maxKeycode;
         keycode >= minKeycode && result.size() < limit; --keycode) {
      bool empty = true;
      for (const auto value : of(static_cast<xcb_keycode_t>(keycode))) {
        empty = empty && value == XCB_NO_SYMBOL;
      }
      if (empty) {
        result.push_back(static_cast<xcb_keycode_t>(keycode));
      }
    }
    return result;
  }
};

KeyboardMap readKeyboardMap(xcb_connection_t *connection) {
  KeyboardMap map;
  const xcb_setup_t *setup = xcb_get_setup(connection);
  const auto count =
      static_cast<uint8_t>(setup->max_keycode - setup->min_keycode + 1);
  ReplyPtr<xcb_get_keyboard_mapping_reply_t> reply(
      xcb_get_keyboard_mapping_reply(
          connection,
          xcb_get_keyboard_mapping(connection, setup->min_keycode, count),
          nullptr));
  if (!reply) {
    return map;
  }
  map.minKeycode = setup->min_keycode;
  map.maxKeycode = setup->max_keycode;
  map.perKeycode = reply->keysyms_per_keycode;
  const auto *keysyms = xcb_get_keyboard_mapping_keysyms(reply.get());
  const int length = xcb_get_keyboard_mapping_keysyms_length(reply.get());
  map.keysyms.assign(keysyms, keysyms + length);
  return map;
}

void tap(xcb_connection_t *connection, xcb_keycode_t keycode,
         uint32_t intervalMs) {
  xcb_test_fake_input(connection, XCB_KEY_PRESS, keycode, XCB_CURRENT_TIME,
                      XCB_NONE, 0, 0, 0);
  xcb_test_fake_input(connection, XCB_KEY_RELEASE, keycode, XCB_CURRENT_TIME,
                      XCB_NONE, 0, 0, 0);
  xcb_flush(connection);
  std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
}

class X11VirtualKeyboard final : public VirtualKeyboardDevice {
public:
  ~X11VirtualKeyboard() override { worker_.stop(); }

  const char *kind() const override { return "x11"; }

  bool isAvailable() override {
    if (!probed_) {
      probed_ = true;
      const char *display = std::getenv("DISPLAY");
      if (display && display[0]) {
        const ConnectionPtr probe = connect();
        available_ = probe && hasXTest(probe.get());
      }
    }
    return available_;
  }

  void replace(const VirtualKeyboardRequest &request, Done done) override {
    worker_.post([this, request, done = std::move(done)] {
      const bool ok = emit(request);
      done(ok);
      // Trả keymap sau khi báo xong để không làm chậm lần thay chữ, nhưng vẫn
      // trước việc kế tiếp trên cùng thread.
      restoreBorrowed();
      if (!ok ||
          (connection_ && xcb_connection_has_error(connection_.get()) != 0)) {
        connection_.reset();
      }
    });
  }

  void cancel() override { worker_.dropPending(); }

private:
  bool emit(const VirtualKeyboardRequest &request) {
    const auto codepoints = utf8Codepoints(request.text);
    if (!request.text.empty() && codepoints.empty()) {
      return false;
    }
    if (!connection_) {
      connection_ = connect();
    }
    if (!connection_) {
      FCITX_ERROR() << "areca: virtual-keyboard cannot connect to X server";
      return false;
    }
    xcb_connection_t *connection = connection_.get();
    const uint32_t intervalMs = std::max<uint32_t>(request.keyIntervalMs, 1);
    const KeyboardMap map = readKeyboardMap(connection);
    const auto backspace = map.find(kVirtualKeyboardBackSpaceKeysym);
    if (!map.valid() || !backspace) {
      FCITX_ERROR() << "areca: virtual-keyboard cannot read X keymap";
      return false;
    }
    for (uint32_t i = 0; i < request.deleteCount; ++i) {
      tap(connection, *backspace, intervalMs);
    }
    if (codepoints.empty()) {
      return xcb_connection_has_error(connection) == 0;
    }

    const auto spares = map.spares(kMaxBorrowedKeycodes);
    if (spares.empty()) {
      FCITX_ERROR() << "areca: virtual-keyboard has no spare X keycode";
      return false;
    }
    // Mỗi ký tự của một lượt có keycode riêng và giữ nguyên suốt lượt; đổi lại
    // một keycode khi ứng dụng chưa xử lý xong phím trước sẽ ra nhầm ký tự.
    size_t position = 0;
    while (position < codepoints.size()) {
      if (position > 0) {
        restoreBorrowed();
      }
      const size_t count =
          std::min(spares.size(), codepoints.size() - position);
      std::vector<xcb_keysym_t> keysyms(map.perKeycode, XCB_NO_SYMBOL);
      for (size_t i = 0; i < count; ++i) {
        keysyms[0] = kUnicodeKeysymBase | codepoints[position + i];
        xcb_change_keyboard_mapping(connection, 1, spares[i], map.perKeycode,
                                    keysyms.data());
        borrowed_.emplace_back(spares[i], map.of(spares[i]));
      }
      xcb_flush(connection);
      for (size_t i = 0; i < count; ++i) {
        tap(connection, spares[i], intervalMs);
      }
      position += count;
    }
    return xcb_connection_has_error(connection) == 0;
  }

  void restoreBorrowed() {
    if (borrowed_.empty() || !connection_) {
      borrowed_.clear();
      return;
    }
    std::this_thread::sleep_for(kRestoreDelay);
    for (const auto &[keycode, original] : borrowed_) {
      xcb_change_keyboard_mapping(connection_.get(), 1, keycode,
                                  static_cast<uint8_t>(original.size()),
                                  original.data());
    }
    xcb_flush(connection_.get());
    borrowed_.clear();
  }

  SerialWorker worker_;
  ConnectionPtr connection_;
  // Keycode đã mượn kèm keysym gốc, trả lại sau mỗi lượt.
  std::vector<std::pair<xcb_keycode_t, std::vector<xcb_keysym_t>>> borrowed_;
  bool probed_ = false;
  bool available_ = false;
};

} // namespace

std::unique_ptr<VirtualKeyboardDevice> createX11VirtualKeyboard() {
  return std::make_unique<X11VirtualKeyboard>();
}

} // namespace areca

#else

namespace areca {

std::unique_ptr<VirtualKeyboardDevice> createX11VirtualKeyboard() {
  return nullptr;
}

} // namespace areca

#endif
