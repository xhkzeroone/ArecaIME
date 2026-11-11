#include "virtual_keyboard_device.h"

#include <algorithm>
#include <array>
#include <cstdio>

#include <fcitx-utils/utf8.h>

namespace areca {
namespace {

// Mã evdev của các phím chữ, số và dấu trên bàn phím PC. Một số ứng dụng
// (Chromium) vẫn nhìn vị trí phím vật lý, nên không gán ký tự vào Backspace,
// Tab, Enter, Esc hay phím bổ trợ.
constexpr std::array<uint32_t, kVirtualKeyboardTextKeys> kTextKeycodes = {
    2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, // 1 … =
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, // q … ]
    30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, // a … `
    43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53,     // \ … /
};

// evdev KEY_BACKSPACE.
constexpr uint32_t kBackspaceKeycode = 14;
// XKB đánh số keycode lệch 8 so với evdev.
constexpr uint32_t kXkbOffset = 8;

std::string keysymName(uint32_t codepoint) {
  std::array<char, 16> buffer{};
  std::snprintf(buffer.data(), buffer.size(),
                codepoint <= 0xffff ? "U%04X" : "U%08X", codepoint);
  return buffer.data();
}

} // namespace

std::vector<uint32_t> utf8Codepoints(std::string_view text) {
  std::vector<uint32_t> codepoints;
  if (!fcitx::utf8::validate(text)) {
    return codepoints;
  }
  for (const auto codepoint : fcitx::utf8::MakeUTF8CharRange(text)) {
    codepoints.push_back(static_cast<uint32_t>(codepoint));
  }
  return codepoints;
}

std::vector<uint32_t> virtualKeyboardEchoSequence(uint32_t deleteCount,
                                                  std::string_view text) {
  std::vector<uint32_t> sequence(deleteCount, 0);
  const auto codepoints = utf8Codepoints(text);
  sequence.insert(sequence.end(), codepoints.begin(), codepoints.end());
  return sequence;
}

uint32_t virtualKeyboardTextKeycode(std::size_t index) {
  return kTextKeycodes.at(index);
}

std::string
buildVirtualKeyboardKeymap(const std::vector<uint32_t> &codepoints) {
  std::string keycodes;
  std::string symbols;
  keycodes +=
      "  <BKSP> = " + std::to_string(kBackspaceKeycode + kXkbOffset) + ";\n";
  symbols += "  key <BKSP> { [ BackSpace ] };\n";
  const std::size_t count = std::min(codepoints.size(), kTextKeycodes.size());
  for (std::size_t i = 0; i < count; ++i) {
    const std::string name = "T" + std::to_string(i);
    keycodes += "  <" + name +
                "> = " + std::to_string(kTextKeycodes[i] + kXkbOffset) + ";\n";
    symbols +=
        "  key <" + name + "> { [ " + keysymName(codepoints[i]) + " ] };\n";
  }
  return "xkb_keymap {\n"
         "xkb_keycodes \"areca\" {\n"
         "  minimum = 8;\n"
         "  maximum = 255;\n" +
         keycodes +
         "};\n"
         "xkb_types \"areca\" { include \"complete\" };\n"
         "xkb_compatibility \"areca\" { include \"complete\" };\n"
         "xkb_symbols \"areca\" {\n" +
         symbols +
         "};\n"
         "};\n";
}

} // namespace areca
