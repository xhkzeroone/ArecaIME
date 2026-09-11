#pragma once

#include <imgui.h>

namespace areca::settings {

// Apply the same paper, green, and orange visual language used by the website.
void applyArecaTheme();

// SDL clear color shown around/between Dear ImGui draw calls.
ImVec4 arecaThemeBackground();

} // namespace areca::settings
