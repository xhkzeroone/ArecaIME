#include "key_mapper.h"

namespace areca::settings {

    std::string formatKeyList(const std::vector<fcitx::Key>& keys) {
        std::string result;
        for (const auto& key : keys) {
            if (!result.empty()) {
                result += ", ";
            }
            result += key.toString();
        }
        return result;
    }

    std::string sdlToFcitx(SDL_Keycode key, SDL_Keymod mod) {
        const bool keyIsModifier =
            (key == SDLK_LCTRL
             || key == SDLK_RCTRL
             || key == SDLK_LALT
             || key == SDLK_RALT
             || key == SDLK_LSHIFT
             || key == SDLK_RSHIFT
             || key == SDLK_LGUI
             || key == SDLK_RGUI);

        const bool hasCtrl = (mod & SDL_KMOD_CTRL) && key != SDLK_LCTRL && key != SDLK_RCTRL;
        const bool hasAlt = (mod & SDL_KMOD_ALT) && key != SDLK_LALT && key != SDLK_RALT;
        const bool hasShift = (mod & SDL_KMOD_SHIFT) && key != SDLK_LSHIFT && key != SDLK_RSHIFT;
        const bool hasSuper = (mod & SDL_KMOD_GUI) && key != SDLK_LGUI && key != SDLK_RGUI;

        if (keyIsModifier && !hasCtrl && !hasAlt && !hasShift && !hasSuper) {
            return "";
        }

        std::string result;
        if (hasCtrl)
            result += "Control+";
        if (hasAlt)
            result += "Alt+";
        if (hasShift)
            result += "Shift+";
        if (hasSuper)
            result += "Super+";

        if (key == SDLK_SPACE)
            result += "space";
        else if (key == SDLK_RETURN)
            result += "Return";
        else if (key == SDLK_BACKSPACE)
            result += "BackSpace";
        else if (key == SDLK_TAB)
            result += "Tab";
        else if (key == SDLK_GRAVE)
            result += "grave";
        else if (key == SDLK_MINUS)
            result += "minus";
        else if (key == SDLK_EQUALS)
            result += "equal";
        else if (key == SDLK_SEMICOLON)
            result += "semicolon";
        else if (key == SDLK_APOSTROPHE)
            result += "apostrophe";
        else if (key == SDLK_COMMA)
            result += "comma";
        else if (key == SDLK_PERIOD)
            result += "period";
        else if (key == SDLK_SLASH)
            result += "slash";
        else if (key == SDLK_BACKSLASH)
            result += "backslash";
        else if (key == SDLK_LEFTBRACKET)
            result += "bracketleft";
        else if (key == SDLK_RIGHTBRACKET)
            result += "bracketright";
        else if (key == SDLK_LSHIFT)
            result += "Shift_L";
        else if (key == SDLK_RSHIFT)
            result += "Shift_R";
        else if (key == SDLK_LCTRL)
            result += "Control_L";
        else if (key == SDLK_RCTRL)
            result += "Control_R";
        else if (key == SDLK_LALT)
            result += "Alt_L";
        else if (key == SDLK_RALT)
            result += "Alt_R";
        else if (key == SDLK_LGUI)
            result += "Super_L";
        else if (key == SDLK_RGUI)
            result += "Super_R";
        else if (key >= SDLK_A && key <= SDLK_Z)
            result += static_cast<char>('a' + (key - SDLK_A));
        else if (key >= SDLK_0 && key <= SDLK_9)
            result += static_cast<char>('0' + (key - SDLK_0));
        else if (key >= SDLK_F1 && key <= SDLK_F12)
            result += "F" + std::to_string(key - SDLK_F1 + 1);
        else
            return "";

        fcitx::Key k(result);
        if (!k.isValid())
            return "";
        return k.toString();
    }

} // namespace areca::settings
