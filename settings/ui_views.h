#pragma once

#include <string>
#include <vector>

#include "config_store.h"

namespace areca::settings {

void drawBasic(ConfigStore &config,
               const std::vector<std::string> &inputMethods,
               const std::vector<std::string> &charsets,
               bool &listeningShortcut);

void drawMacros(ConfigStore &config, size_t &pendingDeleteIndex);

void drawAdvanced(ConfigStore &config);

void drawWindow(ConfigStore &config,
                const std::vector<std::string> &inputMethods,
                const std::vector<std::string> &charsets,
                bool &listeningShortcut, std::string &status, bool &running);

} // namespace areca::settings
