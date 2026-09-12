#include "font_loader.h"

#include <fontconfig/fontconfig.h>
#include <imgui.h>
#include <imgui_impl_sdlrenderer3.h>
#include <string>

namespace areca::settings {

namespace {

std::string findVietnameseFontPath() {
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
        return "";
    }

    std::string fontPath;
    FcChar8* file = nullptr;
    if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch && file != nullptr) {
        fontPath = reinterpret_cast<const char*>(file);
    }
    FcPatternDestroy(match);
    return fontPath;
}

} // namespace

void loadVietnameseFont(float fontSize) {
    std::string path = findVietnameseFontPath();
    if (!path.empty()) {
        ImGui::GetIO().Fonts->AddFontFromFileTTF(
            path.c_str(), fontSize, nullptr, ImGui::GetIO().Fonts->GetGlyphRangesVietnamese()
        );
    } else {
        ImGui::GetIO().Fonts->AddFontDefault();
    }
}

void reloadFont(float fontSize) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    loadVietnameseFont(fontSize);
    io.Fonts->Build();
    ImGui_ImplSDLRenderer3_DestroyDeviceObjects();
}

} // namespace areca::settings
