#include "browser_autocomplete.h"

#include <algorithm>
#include <array>
#include <cstring>

#include <fcitx-utils/utf8.h>

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

std::string normalizedProgramName(std::string program) {
  const auto slash = program.find_last_of('/');
  if (slash != std::string::npos) {
    program.erase(0, slash + 1);
  }
  for (char &c : program) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  constexpr const char suffix[] = ".desktop";
  constexpr size_t suffixLength = sizeof(suffix) - 1;
  if (program.size() >= suffixLength &&
      program.compare(program.size() - suffixLength, suffixLength, suffix) ==
          0) {
    program.resize(program.size() - suffixLength);
  }
  return program;
}

} // namespace

bool isBrowserLikeProgram(const std::string &rawProgram) {
  const std::string program = normalizedProgramName(rawProgram);
  if (program.empty()) {
    return true;
  }

  static constexpr std::array<const char *, 45> patterns = {"chrome",
                                                            "google-chrome",
                                                            "chromium",
                                                            "chromium-browser",
                                                            "edge",
                                                            "msedge",
                                                            "brave",
                                                            "vivaldi",
                                                            "opera",
                                                            "opera-beta",
                                                            "opera-developer",
                                                            "coccoc",
                                                            "yandex",
                                                            "firefox",
                                                            "librewolf",
                                                            "waterfox",
                                                            "floorp",
                                                            "zen",
                                                            "tor-browser",
                                                            "torbrowser",
                                                            "epiphany",
                                                            "falkon",
                                                            "midori",
                                                            "qutebrowser",
                                                            "palemoon",
                                                            "basilisk",
                                                            "nyxt",
                                                            "otter",
                                                            "dooble",
                                                            "arc",
                                                            "helium",
                                                            "mullvad",
                                                            "thorium",
                                                            "ladybird",
                                                            "luakit",
                                                            "surf",
                                                            "min",
                                                            "icecat",
                                                            "seamonkey",
                                                            "konqueror",
                                                            "mercury",
                                                            "angelfish",
                                                            "vieb",
                                                            "netsurf",
                                                            "window:"};
  return std::any_of(patterns.begin(), patterns.end(),
                     [&program](const char *pattern) {
                       return program.find(pattern) != std::string::npos;
                     });
}

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
