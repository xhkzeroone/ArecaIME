#include "config_store.h"

#include <fcitx-config/iniparser.h>
#if __has_include(<fcitx-utils/standardpaths.h>)
#include <fcitx-utils/standardpaths.h>
#define ARECA_SETTINGS_HAS_STANDARD_PATHS 1
#else
#include <fcitx-utils/standardpath.h>
#endif

namespace areca::settings {
    namespace {
#if defined(ARECA_SETTINGS_HAS_STANDARD_PATHS)
        constexpr auto kPkgConfigPath = fcitx::StandardPathsType::PkgConfig;
#else
        constexpr auto kPkgConfigPath = fcitx::StandardPath::Type::PkgConfig;
#endif
        constexpr const char* kMainConfigPath = "conf/areca.conf";
        constexpr const char* kMacroConfigPath = "conf/areca-macro-table.conf";
        constexpr const char* kAdvancedConfigPath = "conf/areca-advanced.conf";
    } // namespace

    void ConfigStore::load() {
        fcitx::readAsIni(main, kPkgConfigPath, kMainConfigPath);
        // Preserve the addon's migration behaviour for installations created before
        // advanced settings were split into their own file.
        advanced.backspaceDelayMs.setValue(main.legacyBackspaceDelayMs.value());
        advanced.afterBackspaceWaitMs.setValue(main.legacyAfterBackspaceWaitMs.value());
        advanced.postCommitDelayMs.setValue(main.legacyPostCommitDelayMs.value());
        fcitx::readAsIni(advanced, kPkgConfigPath, kAdvancedConfigPath);
        fcitx::readAsIni(macros, kPkgConfigPath, kMacroConfigPath);
    }

    void ConfigStore::save() const {
        fcitx::safeSaveAsIni(main, kPkgConfigPath, kMainConfigPath);
        fcitx::safeSaveAsIni(macros, kPkgConfigPath, kMacroConfigPath);
        fcitx::safeSaveAsIni(advanced, kPkgConfigPath, kAdvancedConfigPath);
    }

} // namespace areca::settings
