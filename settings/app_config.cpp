#include "app_config.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

// Tìm đường dẫn ~/.config/fcitx5/conf/
static std::string configFilePath() {
    const char* home = std::getenv("HOME");
    if (!home) {
        home = "/tmp";
    }
    return std::string(home) + "/.config/fcitx5/conf/areca-settings.conf";
}

namespace areca::settings {

const char* themeName(AppTheme theme) {
    switch (theme) {
        case AppTheme::Light:           return "Light";
        case AppTheme::Dark:            return "Dark";
        case AppTheme::ArecaDark:       return "ArecaDark";
        case AppTheme::Gruvbox:         return "Gruvbox";
        case AppTheme::Catppuccin:      return "Catppuccin";
        case AppTheme::CatppuccinLatte: return "CatppuccinLatte";
        case AppTheme::Dracula:         return "Dracula";
        case AppTheme::OneDark:         return "OneDark";
        case AppTheme::TokyoNight:      return "TokyoNight";
    }
    return "Light";
}

void AppConfig::load() {
    std::ifstream file(configFilePath());
    if (!file.is_open()) {
        return;
    }
    std::string line;
    while (std::getline(file, line)) {
        // Bỏ comment và dòng trống
        if (line.empty() || line[0] == '#' || line[0] == '[') {
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key   = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        // Trim whitespace
        const auto trimLeft  = [](std::string& s) { s.erase(0, s.find_first_not_of(" \t")); };
        const auto trimRight = [](std::string& s) {
            const auto pos = s.find_last_not_of(" \t\r\n");
            if (pos != std::string::npos) {
                s.erase(pos + 1);
            }
        };
        trimLeft(key);
        trimRight(key);
        trimLeft(value);
        trimRight(value);

        if (key == "Theme") {
            if      (value == "Dark")            { theme = AppTheme::Dark; }
            else if (value == "ArecaDark")       { theme = AppTheme::ArecaDark; }
            else if (value == "Gruvbox")         { theme = AppTheme::Gruvbox; }
            else if (value == "Catppuccin")      { theme = AppTheme::Catppuccin; }
            else if (value == "CatppuccinLatte") { theme = AppTheme::CatppuccinLatte; }
            else if (value == "Dracula")         { theme = AppTheme::Dracula; }
            else if (value == "OneDark")         { theme = AppTheme::OneDark; }
            else if (value == "TokyoNight")      { theme = AppTheme::TokyoNight; }
            else                                 { theme = AppTheme::Light; }
        } else if (key == "FontSize") {
            try {
                const float parsed = std::stof(value);
                fontSize = std::clamp(parsed, kMinFontSize, kMaxFontSize);
            } catch (...) {}
        }
    }
}

void AppConfig::save() const {
    const std::string path = configFilePath();

    // Đảm bảo thư mục tồn tại
    const auto dirEnd = path.rfind('/');
    if (dirEnd != std::string::npos) {
        const std::string dir = path.substr(0, dirEnd);
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
    }

    std::ofstream file(path);
    if (!file.is_open()) {
        return;
    }
    file << "[Appearance]\n";
    file << "Theme=" << themeName(theme) << "\n";
    file << "FontSize=" << fontSize << "\n";
}

} // namespace areca::settings
