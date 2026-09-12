#pragma once

#include <string>

namespace areca::settings {

enum class AppTheme {
    Light,           // Areca Light (mặc định)
    Dark,            // Dark (đen xám trung tính)
    ArecaDark,       // Areca Dark (charcoal xanh lá)
    Gruvbox,         // Gruvbox Dark
    Catppuccin,      // Catppuccin Mocha
    CatppuccinLatte, // Catppuccin Latte (sáng)
    Dracula,         // Dracula
    OneDark,         // One Dark
    TokyoNight,      // Tokyo Night
};

constexpr float kDefaultFontSize = 18.0F;
constexpr float kMinFontSize = 12.0F;
constexpr float kMaxFontSize = 24.0F;

// Config giao diện của settings app, lưu tại ~/.config/fcitx5/conf/areca-settings.conf.
// Tách biệt hoàn toàn khỏi config engine Areca.
struct AppConfig {
    AppTheme theme = AppTheme::Light;
    float    fontSize = kDefaultFontSize;

    void load();
    void save() const;
};

// Tên hiển thị cho từng theme.
const char* themeName(AppTheme theme);

} // namespace areca::settings
