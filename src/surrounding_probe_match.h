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

  // Đáng lẽ không còn gì trước con trỏ: text khác rỗng là update trễ từ trước
  // lệnh xoá. Thiếu điều kiện này, ends_with("") bên dưới nhận mọi update.
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

// Đoạn text Areca biết chắc đang nằm ngay trước con trỏ chỉ cần phần đuôi để
// so với snapshot, nên giới hạn kích thước.
inline constexpr std::size_t kMaxKnownTextBytes = 256;

// Cập nhật đoạn text đã biết sau khi Areca xoá `deleteCount` ký tự trước con
// trỏ rồi chèn `inserted`. Xoá nhiều hơn phần đã biết thì chỉ còn chắc phần vừa
// chèn.
inline void updateKnownTextBeforeCursor(std::string &known,
                                        uint32_t deleteCount,
                                        std::string_view inserted) {
  if (deleteCount > 0) {
    const auto length = fcitx::utf8::lengthValidated(known);
    if (length == fcitx::utf8::INVALID_LENGTH || length <= deleteCount) {
      known.clear();
    } else {
      const auto keepBytes = fcitx::utf8::ncharByteLength(
          known.begin(), static_cast<int>(length - deleteCount));
      known.erase(static_cast<std::size_t>(keepBytes));
    }
  }
  known.append(inserted);
  if (known.size() > kMaxKnownTextBytes) {
    // Cắt ở biên ký tự UTF-8 để vẫn so sánh được phần đuôi.
    std::size_t cut = known.size() - kMaxKnownTextBytes;
    while (cut < known.size() &&
           (static_cast<unsigned char>(known[cut]) & 0xC0U) == 0x80U) {
      ++cut;
    }
    known.erase(0, cut);
  }
}

// Snapshot có kết thúc bằng `tail` không, tức frontend đã báo đủ những gì
// Areca biết chắc đang nằm trước con trỏ. Frontend trễ một nhịp (báo "g" khi ô
// đã là "go") không qua được kiểm tra này. `tail` rỗng nghĩa là không biết nên
// luôn đúng.
inline bool surroundingSnapshotEndsWith(bool valid, std::string_view text,
                                        uint32_t cursor, uint32_t anchor,
                                        std::string_view tail) {
  if (tail.empty()) {
    return true;
  }
  if (!valid || cursor != anchor) {
    return false;
  }
  std::string_view before;
  if (!textBeforeCursor(text, cursor, before)) {
    return false;
  }
  if (before.ends_with(tail)) {
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
  return std::string_view(normalize(before)).ends_with(normalize(tail));
}

} // namespace areca
