#pragma once

#include <string>

namespace areca {

// Chỉ xác nhận selection thuộc về composition khi nó bắt đầu ngay sau toàn bộ
// text Bamboo đang hiển thị. Cursor/anchor lệch nhưng cắt vào giữa từ là
// snapshot stale và không được dùng để cộng thêm Backspace.
bool isSelectionImmediatelyAfterText(const std::string &surroundingText,
                                     unsigned int cursor,
                                     unsigned int anchor,
                                     const std::string &shownText);

bool isCollapsedFirefoxAutocomplete(const std::string &program,
                                    const std::string &surroundingText,
                                    unsigned int cursor, unsigned int anchor,
                                    const std::string &shownText);

bool isBrowserAutocomplete(const std::string &surroundingText,
                           unsigned int cursor, unsigned int anchor,
                           const std::string &shownText);

} // namespace areca
