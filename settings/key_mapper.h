#pragma once

#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <fcitx-utils/key.h>

namespace areca::settings {

std::string formatKeyList(const std::vector<fcitx::Key> &keys);
std::string sdlToFcitx(SDL_Keycode key, SDL_Keymod mod);

} // namespace areca::settings
