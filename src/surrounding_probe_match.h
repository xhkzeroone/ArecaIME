#pragma once

#include <cstdint>
#include <string_view>

#include <fcitx-utils/utf8.h>

namespace areca {

inline bool textBeforeCursor(std::string_view text, uint32_t cursor,
                             std::string_view &before) {
  if (!fcitx::utf8::validate(text) || cursor > fcitx::utf8::length(text)) {
    return false;
  }
  if (cursor == 0) {
    before = {};
    return true;
  }
  const auto bytes = fcitx::utf8::ncharByteLength(text.begin(), cursor);
  if (bytes < 0) {
    return false;
  }
  before = text.substr(0, static_cast<std::size_t>(bytes));
  return true;
}

inline bool
surroundingProbeMatches(bool actualValid, std::string_view originalText,
                        uint32_t originalCursor, std::string_view expectedText,
                        uint32_t expectedCursor, std::string_view actualText,
                        uint32_t actualCursor, uint32_t actualAnchor) {
  if (actualCursor != actualAnchor) {
    return false;
  }

  std::string_view originalBefore;
  std::string_view expectedBefore;
  std::string_view actualBefore;
  if (!textBeforeCursor(originalText, originalCursor, originalBefore) ||
      !textBeforeCursor(expectedText, expectedCursor, expectedBefore) ||
      !textBeforeCursor(actualText, actualCursor, actualBefore) ||
      !originalBefore.starts_with(expectedBefore)) {
    return false;
  }

  const std::string_view deleted = originalBefore.substr(expectedBefore.size());
  if (deleted.empty()) {
    return false;
  }

  // This matcher is called only for an actual SurroundingTextUpdated event.
  // Some clients invalidate the snapshot when the field becomes empty.
  if (!actualValid) {
    return actualText.empty() && actualCursor == 0 && expectedBefore.empty();
  }

  // The text that was supposed to be deleted is still immediately before the
  // cursor, so this is the old snapshot.
  if (actualBefore.ends_with(originalBefore)) {
    return false;
  }
  if (!actualBefore.ends_with(expectedBefore)) {
    return false;
  }

  // Some clients move the cursor first and remove the character afterward.
  // Do not mistake that intermediate state for a completed deletion.
  if (actualCursor < fcitx::utf8::length(actualText)) {
    const auto cursorByte =
        fcitx::utf8::ncharByteLength(actualText.begin(), actualCursor);
    const auto firstDeletedBytes =
        fcitx::utf8::ncharByteLength(deleted.begin(), 1);
    if (cursorByte >= 0 && firstDeletedBytes > 0 &&
        actualText.substr(static_cast<std::size_t>(cursorByte),
                          static_cast<std::size_t>(firstDeletedBytes)) ==
            deleted.substr(0, static_cast<std::size_t>(firstDeletedBytes))) {
      return false;
    }
  }

  return true;
}

} // namespace areca
