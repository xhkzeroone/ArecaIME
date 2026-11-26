#include "surrounding_text_cache.h"

#include <algorithm>

#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/inputcontext.h>
#include <fcitx/surroundingtext.h>

namespace areca {

void updateSurroundingCacheAfterDelete(fcitx::InputContext &inputContext,
                                       int offset, uint32_t count) {
  if (!inputContext.capabilityFlags().test(
          fcitx::CapabilityFlag::SurroundingText)) {
    return;
  }
  auto &st = inputContext.surroundingText();
  if (!st.isValid()) {
    return;
  }
  st.deleteText(offset, count);
}

void updateSurroundingCacheAfterCommit(fcitx::InputContext &inputContext,
                                       const std::string &committedText) {
  if (committedText.empty() || !inputContext.capabilityFlags().test(
                                   fcitx::CapabilityFlag::SurroundingText)) {
    return;
  }
  auto &st = inputContext.surroundingText();
  if (!st.isValid()) {
    return;
  }

  const auto &text = st.text();
  const unsigned int cursor = st.cursor();

  const size_t cursorBytes = fcitx::utf8::ncharByteLength(text.begin(), cursor);
  std::string newText;
  newText.reserve(text.size() + committedText.size());
  newText.append(text, 0, cursorBytes);
  newText.append(committedText);
  newText.append(text, cursorBytes);

  const unsigned int committedChars =
      static_cast<unsigned int>(fcitx::utf8::length(committedText));
  const unsigned int newCursor = cursor + committedChars;
  st.setText(newText, newCursor, newCursor);
}

std::size_t commonPrefixBytesUTF8Boundary(const std::string &s1,
                                          const std::string &s2) {
  std::size_t n = std::min(s1.size(), s2.size());
  std::size_t i = 0;
  while (i < n && s1[i] == s2[i]) {
    ++i;
  }
  while (i > 0 && i < s1.size() &&
         (static_cast<unsigned char>(s1[i]) & 0xC0u) == 0x80u) {
    --i;
  }
  return i;
}

uint32_t utf8CharCount(const std::string &s) {
  if (!fcitx::utf8::validate(s)) {
    return 0;
  }
  return static_cast<uint32_t>(fcitx::utf8::length(s));
}

} // namespace areca
