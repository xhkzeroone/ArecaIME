#pragma once

#include <cstdint>
#include <string>
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

  // When the deletion should leave nothing before the cursor, any non-empty
  // text is unrelated (e.g. a lagging update from before the delete). Without
  // this, ends_with("") below would accept every update.
  if (expectedBefore.empty() && !actualBefore.empty()) {
    return false;
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

// Returns false when the surrounding snapshot cannot reflect what Areca has
// already committed for the current word: the text before the cursor must end
// with the word Bamboo is composing. A frontend that lags one update behind
// (reports "g" while the field shows "go") fails this check, and deleting
// relative to such a snapshot is not trustworthy.
inline bool surroundingSnapshotMatchesWord(bool valid, std::string_view text,
                                           uint32_t cursor, uint32_t anchor,
                                           std::string_view word) {
  if (word.empty()) {
    return true;
  }
  if (!valid || cursor != anchor) {
    return false;
  }
  std::string_view before;
  if (!textBeforeCursor(text, cursor, before)) {
    return false;
  }
  if (before.ends_with(word)) {
    return true;
  }
  // Chromium báo dấu cách cuối dòng thành NBSP (U+00A0). Coi hai ký tự này là
  // một khi so phần đuôi để không nhận nhầm là snapshot trễ.
  constexpr std::string_view kNbsp = "\xC2\xA0";
  const auto normalize = [&](std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (std::size_t i = 0; i < input.size();) {
      if (input.substr(i, kNbsp.size()) == kNbsp) {
        out.push_back(' ');
        i += kNbsp.size();
      } else {
        out.push_back(input[i++]);
      }
    }
    return out;
  };
  return std::string_view(normalize(before)).ends_with(normalize(word));
}

} // namespace areca
