#pragma once

#include "app_config.h"

#include <imgui.h>

namespace areca::settings {

// Apply theme tương ứng với AppTheme.
void applyTheme(AppTheme theme);

// SDL clear color phù hợp với theme đang dùng.
ImVec4 themeBackground(AppTheme theme);

} // namespace areca::settings
