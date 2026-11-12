#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace areca {

// Mã Unicode của từng ký tự trong chuỗi UTF-8; chuỗi lỗi trả về rỗng.
std::vector<uint32_t> utf8Codepoints(std::string_view text);

// Phím ảo đi vào ứng dụng qua đường bàn phím nên quay lại fcitx5 như phím người
// dùng gõ. Danh sách này theo đúng thứ tự phát: 0 là Backspace, còn lại là mã
// Unicode của từng ký tự.
std::vector<uint32_t> virtualKeyboardEchoSequence(uint32_t deleteCount,
                                                  std::string_view text);

// Mỗi keymap chỉ chở được chừng này ký tự (26 chữ cái + 10 chữ số); chuỗi dài
// hơn được chia thành nhiều đợt, mỗi đợt một keymap.
inline constexpr std::size_t kVirtualKeyboardCarrierKeys = 36;

// Mã evdev của phím chở ký tự thứ `slot` trong một đợt.
uint32_t virtualKeyboardCarrierKeycode(std::size_t slot);

// Chia chuỗi thành các đợt, mỗi đợt tối đa kVirtualKeyboardCarrierKeys ký tự.
// Luôn có ít nhất một đợt (có thể rỗng) để lượt chỉ xoá vẫn có keymap.
std::vector<std::vector<uint32_t>>
splitVirtualKeyboardBursts(const std::vector<uint32_t> &codepoints);

// Keymap XKB của một đợt: phím Backspace và các phím chở ký tự của đợt đó.
std::string buildVirtualKeyboardKeymap(const std::vector<uint32_t> &burst);

} // namespace areca
