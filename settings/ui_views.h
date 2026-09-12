#pragma once

#include <string>
#include <vector>

#include "app_config.h"
#include "config_store.h"

namespace areca::settings {

void drawAppearance(AppConfig& appConfig, bool& needReapplyTheme);

void drawBasic(
    ConfigStore& config, const std::vector<std::string>& inputMethods, const std::vector<std::string>& charsets,
    bool& listeningShortcut
);

bool drawMacros(ConfigStore& config, size_t& pendingDeleteIndex);

void drawAdvanced(ConfigStore& config);

void drawWindow(
    ConfigStore& config, AppConfig& appConfig, const std::vector<std::string>& inputMethods,
    const std::vector<std::string>& charsets, bool& listeningShortcut, std::string& status, bool& running,
    bool& needReapplyTheme
);

} // namespace areca::settings
