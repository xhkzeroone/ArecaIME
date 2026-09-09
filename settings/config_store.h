#pragma once

#include "areca_config.h"

namespace areca::settings {

// Uses the exact same Fcitx configuration schema and paths as the addon.
// Keeping this outside the engine lets the settings process run independently.
class ConfigStore {
  public:
    void load();
    void save() const;

    ArecaConfig main;
    MacroTableConfig macros;
    AdvancedConfig advanced;
};

} // namespace areca::settings
