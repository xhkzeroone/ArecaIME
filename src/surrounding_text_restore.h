#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace areca {

struct SurroundingRestoreCandidate {
  std::string text;
  uint32_t characterCount = 0;
};

// Lấy đúng phần từ nằm ngay trước con trỏ để nạp lại vào Bamboo. Hàm không
// thay đổi InputContext; mọi trường hợp mơ hồ như selection, UTF-8 lỗi hoặc từ
// dài quá giới hạn đều trả về nullopt để phím tiếp tục theo luồng bình thường.
std::optional<SurroundingRestoreCandidate>
extractSurroundingRestoreCandidate(const std::string &text, uint32_t cursor,
                                   uint32_t anchor,
                                   std::size_t maxCharacters = 16);

} // namespace areca
