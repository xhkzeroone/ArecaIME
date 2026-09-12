#include "surrounding_text_restore.h"

#include <string_view>
#include <vector>

#include <fcitx-utils/utf8.h>

namespace areca {
namespace {

bool isVietnameseWordCharacter(uint32_t codepoint) {
  // Bản đầu chỉ nhập lại Unicode dựng sẵn. Bamboo phải so sánh chính xác chuỗi
  // hiển thị trước khi cho phép sửa nên dấu tổ hợp không được nhận ở đây.
  static const std::u32string_view vietnameseLetters =
      U"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
      U"ÀÁẢÃẠÂẦẤẨẪẬĂẰẮẲẴẶÈÉẺẼẸÊỀẾỂỄỆÌÍỈĨỊ"
      U"ÒÓỎÕỌÔỒỐỔỖỘƠỜỚỞỠỢÙÚỦŨỤƯỪỨỬỮỰỲÝỶỸỴĐ"
      U"àáảãạâầấẩẫậăằắẳẵặèéẻẽẹêềếểễệìíỉĩị"
      U"òóỏõọôồốổỗộơờớởỡợùúủũụưừứửữựỳýỷỹỵđ";
  return vietnameseLetters.find(static_cast<char32_t>(codepoint)) !=
         std::u32string_view::npos;
}

bool isCombiningMark(uint32_t codepoint) {
  return codepoint >= 0x0300 && codepoint <= 0x036F;
}

} // namespace

std::optional<SurroundingRestoreCandidate>
extractSurroundingRestoreCandidate(const std::string &text, uint32_t cursor,
                                   uint32_t anchor,
                                   std::size_t maxCharacters) {
  if (cursor != anchor || !cursor || !maxCharacters ||
      !fcitx::utf8::validate(text)) {
    return std::nullopt;
  }

  std::vector<uint32_t> characters;
  std::vector<std::size_t> byteOffsets;
  auto iterator = text.begin();
  while (iterator != text.end()) {
    byteOffsets.push_back(static_cast<std::size_t>(iterator - text.begin()));
    uint32_t codepoint = 0;
    iterator = fcitx::utf8::getNextChar(iterator, text.end(), &codepoint);
    if (!fcitx::utf8::isValidChar(codepoint)) {
      return std::nullopt;
    }
    characters.push_back(codepoint);
  }
  byteOffsets.push_back(text.size());

  if (cursor > characters.size()) {
    return std::nullopt;
  }

  const auto accepted = [](uint32_t codepoint) {
    return isVietnameseWordCharacter(codepoint);
  };

  std::size_t begin = cursor;
  while (begin > 0 && cursor - begin < maxCharacters &&
         accepted(characters[begin - 1])) {
    --begin;
  }
  if (begin == cursor) {
    return std::nullopt;
  }

  // Nếu ký tự ngay trước phần vừa quét là dấu tổ hợp thì phần đuôi đang nằm
  // trong một từ Unicode phân rã. Từ chối toàn bộ thay vì phục hồi một mảnh từ.
  if (begin > 0 && isCombiningMark(characters[begin - 1])) {
    return std::nullopt;
  }

  // Không lấy riêng phần đuôi của một từ dài. Một trạng thái Bamboo bị cắt cụt
  // có thể tính sai số ký tự cần xóa và làm hỏng nội dung trước con trỏ.
  if (cursor - begin == maxCharacters && begin > 0 &&
      accepted(characters[begin - 1])) {
    return std::nullopt;
  }

  SurroundingRestoreCandidate candidate;
  candidate.text =
      text.substr(byteOffsets[begin], byteOffsets[cursor] - byteOffsets[begin]);
  candidate.characterCount = static_cast<uint32_t>(cursor - begin);
  return candidate;
}

} // namespace areca
