#include "font_loader.h"

#include <fontconfig/fontconfig.h>
#include <imgui.h>

namespace areca::settings {

    void loadVietnameseFont() {
        FcInit();
        FcPattern* pattern = FcPatternCreate();
        FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8*>("sans-serif"));
        FcPatternAddString(pattern, FC_LANG, reinterpret_cast<const FcChar8*>("vi"));
        FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
        FcDefaultSubstitute(pattern);
        FcResult result = FcResultNoMatch;
        FcPattern* match = FcFontMatch(nullptr, pattern, &result);
        FcPatternDestroy(pattern);
        if (!match) {
            return;
        }

        FcChar8* file = nullptr;
        if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch) {
            // Vietnamese letters span Latin-1, Latin Extended and Vietnamese
            // precomposed characters. The embedded Dear ImGui default font does not.
            ImGui::GetIO().Fonts->AddFontFromFileTTF(
                reinterpret_cast<const char*>(file), 18.0F, nullptr, ImGui::GetIO().Fonts->GetGlyphRangesVietnamese()
            );
        }
        FcPatternDestroy(match);
    }

} // namespace areca::settings
