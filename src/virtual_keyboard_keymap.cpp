#include "virtual_keyboard_keymap.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>

#include <fcitx-utils/utf8.h>
#include <linux/input-event-codes.h>

namespace areca {
namespace {

// Ký tự được chở trên các phím chữ cái rồi chữ số: ứng dụng xem đây là phím gõ
// chữ bình thường, và chúng không đụng tới Backspace hay phím bổ trợ.
constexpr std::array<uint32_t, kVirtualKeyboardCarrierKeys> kCarrierKeys = {
    KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I,
    KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R,
    KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z, KEY_1,
    KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0,
};

// Keycode XKB = mã evdev + 8.
uint32_t xkbKeycode(uint32_t evdev) { return evdev + 8; }

// Keysym Unicode viết dạng số: 0x01000000 + mã ký tự.
std::string unicodeKeysym(uint32_t codepoint) {
  std::ostringstream out;
  out << "0x" << std::hex << (0x01000000u + codepoint);
  return out.str();
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

uint32_t virtualKeyboardCarrierKeycode(std::size_t slot) {
  return kCarrierKeys.at(slot);
}

std::vector<std::vector<uint32_t>>
splitVirtualKeyboardBursts(const std::vector<uint32_t> &codepoints) {
  std::vector<std::vector<uint32_t>> bursts;
  for (auto it = codepoints.begin(); it != codepoints.end();) {
    const auto left = static_cast<std::size_t>(codepoints.end() - it);
    const auto take = static_cast<std::ptrdiff_t>(
        std::min(left, kVirtualKeyboardCarrierKeys));
    bursts.emplace_back(it, it + take);
    it += take;
  }
  if (bursts.empty()) {
    bursts.emplace_back();
  }
  return bursts;
}

std::string buildVirtualKeyboardKeymap(const std::vector<uint32_t> &burst) {
  const std::size_t slots = std::min(burst.size(), kCarrierKeys.size());
  std::ostringstream keycodes;
  std::ostringstream symbols;
  keycodes << "    <BKSP> = " << xkbKeycode(KEY_BACKSPACE) << ";\n";
  symbols << "    key <BKSP> { [ BackSpace ] };\n";
  for (std::size_t slot = 0; slot < slots; ++slot) {
    std::ostringstream name;
    name << "C" << std::setw(2) << std::setfill('0') << slot;
    keycodes << "    <" << name.str()
             << "> = " << xkbKeycode(kCarrierKeys[slot]) << ";\n";
    symbols << "    key <" << name.str() << "> { [ "
            << unicodeKeysym(burst[slot]) << " ] };\n";
  }

  std::ostringstream keymap;
  keymap << "xkb_keymap {\n"
         << "  xkb_keycodes \"areca\" {\n"
         << "    minimum = 8;\n"
         << "    maximum = 255;\n"
         << keycodes.str() << "  };\n"
         << "  xkb_types \"areca\" { include \"complete\" };\n"
         << "  xkb_compatibility \"areca\" { include \"complete\" };\n"
         << "  xkb_symbols \"areca\" {\n"
         << symbols.str() << "  };\n"
         << "};\n";
  return keymap.str();
}

} // namespace areca
