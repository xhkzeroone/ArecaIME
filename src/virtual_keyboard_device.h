#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace areca {

// Thay chữ bằng phím ảo trên một kênh duy nhất: xoá `deleteCount` ký tự bằng
// Backspace rồi gõ `text`. Ký tự không có trên bàn phím được gán tạm vào
// keycode rảnh qua keymap, nên không cần commitString và thứ tự xoá/chèn do
// chính compositor giữ.
struct VirtualKeyboardRequest {
  uint32_t deleteCount = 0;
  std::string text;
};

class VirtualKeyboardDevice {
public:
  // Gọi trên thread của thiết bị khi đã phát xong hoặc thất bại.
  using Done = std::function<void(bool ok)>;

  virtual ~VirtualKeyboardDevice() = default;
  virtual const char *kind() const = 0;
  // Dò một lần rồi nhớ kết quả. Lần đầu có thể mở kết nối tới display.
  virtual bool isAvailable() = 0;
  virtual void replace(const VirtualKeyboardRequest &request, Done done) = 0;
  // Bỏ việc chưa chạy; dùng khi huỷ addon.
  virtual void cancel() = 0;
};

// Trả nullptr khi bản build không có phần tương ứng.
std::unique_ptr<VirtualKeyboardDevice> createWaylandVirtualKeyboard();

// Phím ảo đi vào ứng dụng qua đường bàn phím nên quay lại fcitx5 như phím người
// dùng gõ. Danh sách này theo đúng thứ tự phát: 0 là Backspace, còn lại là mã
// Unicode của từng ký tự.
std::vector<uint32_t> virtualKeyboardEchoSequence(uint32_t deleteCount,
                                                  std::string_view text);

// Mã Unicode của từng ký tự trong chuỗi UTF-8; chuỗi lỗi trả về rỗng.
std::vector<uint32_t> utf8Codepoints(std::string_view text);

// Số keycode tối đa gán được trong một keymap Wayland; chuỗi dài hơn được chia
// thành nhiều lượt.
inline constexpr std::size_t kVirtualKeyboardTextKeys = 47;

// Mã evdev dành cho ký tự thứ `index` trong một lượt keymap.
uint32_t virtualKeyboardTextKeycode(std::size_t index);

// Keymap XKB cho một lượt: Backspace cố định, thêm tối đa
// kVirtualKeyboardTextKeys ký tự gán vào các phím chữ/số.
std::string buildVirtualKeyboardKeymap(const std::vector<uint32_t> &codepoints);

} // namespace areca
