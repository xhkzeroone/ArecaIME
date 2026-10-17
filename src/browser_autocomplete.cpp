#include "browser_autocomplete.h"

#include <algorithm>
#include <array>
#include <cstring>

#include <fcitx-utils/utf8.h>

#include "program_compatibility.h"

namespace areca {
namespace {

size_t utf8ByteOffsetForCharIndex(const std::string &text, size_t charIndex) {
  const auto length = fcitx::utf8::length(text);
  if (charIndex >= length) {
    return text.size();
  }
  const auto it = fcitx::utf8::nextNChar(text.begin(), charIndex);
  return static_cast<size_t>(std::distance(text.begin(), it));
}

size_t utf8CharIndexForByteOffset(const std::string &text, size_t byteOffset) {
  byteOffset = std::min(byteOffset, text.size());
  return fcitx::utf8::length(
      std::string(text.begin(), text.begin() + byteOffset));
}

} // namespace

bool isSelectionImmediatelyAfterText(const std::string &text,
                                     unsigned int cursor,
                                     unsigned int anchor,
                                     const std::string &shownText) {
  if (cursor == anchor || shownText.empty() || !fcitx::utf8::validate(text) ||
      !fcitx::utf8::validate(shownText)) {
    return false;
  }

  const size_t textLength = fcitx::utf8::length(text);
  const size_t shownLength = fcitx::utf8::length(shownText);
  const size_t selectionStart = std::min(cursor, anchor);
  if (cursor > textLength || anchor > textLength ||
      shownLength > selectionStart) {
    return false;
  }

  const size_t shownStart = selectionStart - shownLength;
  const size_t beginByte = utf8ByteOffsetForCharIndex(text, shownStart);
  const size_t endByte = utf8ByteOffsetForCharIndex(text, selectionStart);
  return text.compare(beginByte, endByte - beginByte, shownText) == 0;
}

bool isCollapsedFirefoxAutocomplete(const std::string &program,
                                    const std::string &text,
                                    unsigned int cursor, unsigned int anchor,
                                    const std::string &shownText) {
  if (!isFirefoxFamilyProgram(program) || cursor != anchor ||
      shownText.empty() || !fcitx::utf8::validate(text) ||
      !fcitx::utf8::validate(shownText)) {
    return false;
  }

  const size_t textLength = fcitx::utf8::length(text);
  const size_t shownLength = fcitx::utf8::length(shownText);
  if (cursor != shownLength || cursor >= textLength ||
      text.compare(0, shownText.size(), shownText) != 0) {
    return false;
  }

  const size_t suffixByte = utf8ByteOffsetForCharIndex(text, cursor);
  return std::all_of(text.begin() + suffixByte, text.end(), [](char c) {
    const unsigned char ch = static_cast<unsigned char>(c);
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') ||
           (ch >= 'A' && ch <= 'Z') ||
           std::strchr("-._~:/?#[]@!$&'()*+,;=%", ch) != nullptr;
  });
}

bool isBrowserAutocomplete(const std::string &text, unsigned int cursor,
                           unsigned int anchor,
                           const std::string &shownText) {
  if (!isSelectionImmediatelyAfterText(text, cursor, anchor, shownText)) {
    return false;
  }

  // Autocomplete hợp lệ phải chọn phần suffix ngay sau composition cho tới hết
  // dòng; selection đi qua newline hoặc dừng giữa dòng là selection thông thường.
  const size_t textLength = fcitx::utf8::length(text);
  const unsigned int selectionStart = std::min(cursor, anchor);
  const unsigned int selectionEnd = std::max(cursor, anchor);
  const size_t selectionStartByte =
      utf8ByteOffsetForCharIndex(text, selectionStart);
  const size_t selectionEndByte =
      utf8ByteOffsetForCharIndex(text, selectionEnd);
  const size_t nextLineBreak = text.find('\n', selectionStartByte);
  const size_t lineEnd =
      nextLineBreak == std::string::npos
          ? textLength
          : utf8CharIndexForByteOffset(text, nextLineBreak);
  const bool crossesNewline = nextLineBreak != std::string::npos &&
                              nextLineBreak < selectionEndByte;

  return selectionEnd == lineEnd && !crossesNewline;
}

} // namespace areca
