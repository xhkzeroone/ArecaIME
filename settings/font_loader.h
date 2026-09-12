#pragma once

namespace areca::settings {

constexpr float kDefaultFontBaseSize = 18.0F;

void loadVietnameseFont(float fontSize = kDefaultFontBaseSize);
void reloadFont(float fontSize);

} // namespace areca::settings
