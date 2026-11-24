#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace fcitx {
class InputContext;
}

namespace areca {

void updateSurroundingCacheAfterDelete(fcitx::InputContext &inputContext,
                                       int offset, uint32_t count);
void updateSurroundingCacheAfterCommit(fcitx::InputContext &inputContext,
                                       const std::string &committedText);

std::size_t commonPrefixBytesUTF8Boundary(const std::string &s1,
                                          const std::string &s2);
uint32_t utf8CharCount(const std::string &s);

} // namespace areca
