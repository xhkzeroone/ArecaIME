#include "ui_theme.h"

namespace areca::settings {
namespace {

ImVec4 rgb(const int red, const int green, const int blue, const float alpha = 1.0F) {
    constexpr float channelScale = 1.0F / 255.0F;
    return ImVec4(
        static_cast<float>(red) * channelScale,
        static_cast<float>(green) * channelScale,
        static_cast<float>(blue) * channelScale,
        alpha
    );
}

// ─── Shared layout / spacing (áp dụng cho mọi theme) ───────────────────────

void applyCommonLayout(ImGuiStyle& style) {
    style.WindowPadding        = ImVec2(18.0F, 16.0F);
    style.FramePadding         = ImVec2(10.0F, 7.0F);
    style.ItemSpacing          = ImVec2(10.0F, 9.0F);
    style.ItemInnerSpacing     = ImVec2(8.0F, 6.0F);
    style.CellPadding          = ImVec2(10.0F, 8.0F);
    style.ScrollbarSize        = 13.0F;
    style.GrabMinSize          = 12.0F;

    style.WindowRounding       = 0.0F;
    style.ChildRounding        = 10.0F;
    style.PopupRounding        = 10.0F;
    style.FrameRounding        = 8.0F;
    style.ScrollbarRounding    = 8.0F;
    style.GrabRounding         = 6.0F;
    style.TabRounding          = 8.0F;
    style.MenuItemRounding     = 6.0F;

    style.WindowBorderSize     = 0.0F;
    style.ChildBorderSize      = 0.0F;
    style.PopupBorderSize      = 1.0F;
    style.FrameBorderSize      = 1.0F;
    style.TabBorderSize        = 0.0F;
    style.TabBarBorderSize     = 1.0F;
    style.TabBarOverlineSize   = 3.0F;
    style.TabMinWidthBase      = 118.0F;
    style.SeparatorTextBorderSize = 1.0F;
    style.SeparatorTextPadding = ImVec2(12.0F, 7.0F);
}

// ─── Light (Areca Paper) ────────────────────────────────────────────────────

void applyLightTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsLight(&style);
    applyCommonLayout(style);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                   = rgb(16, 33, 28);
    colors[ImGuiCol_TextDisabled]           = rgb(95, 112, 106);
    colors[ImGuiCol_WindowBg]               = rgb(247, 245, 237);
    colors[ImGuiCol_ChildBg]                = rgb(247, 245, 237);
    colors[ImGuiCol_PopupBg]                = rgb(255, 254, 248);
    colors[ImGuiCol_Border]                 = rgb(16, 33, 28, 0.18F);
    colors[ImGuiCol_BorderShadow]           = rgb(7, 24, 19, 0.0F);

    colors[ImGuiCol_FrameBg]                = rgb(255, 254, 248);
    colors[ImGuiCol_FrameBgHovered]         = rgb(232, 242, 233);
    colors[ImGuiCol_FrameBgActive]          = rgb(218, 235, 223);
    colors[ImGuiCol_TitleBg]                = rgb(236, 234, 221);
    colors[ImGuiCol_TitleBgActive]          = rgb(218, 235, 223);
    colors[ImGuiCol_TitleBgCollapsed]       = rgb(236, 234, 221);
    colors[ImGuiCol_MenuBarBg]              = rgb(247, 245, 237);

    colors[ImGuiCol_ScrollbarBg]            = rgb(236, 234, 221, 0.72F);
    colors[ImGuiCol_ScrollbarGrab]          = rgb(159, 177, 168);
    colors[ImGuiCol_ScrollbarGrabHovered]   = rgb(95, 112, 106);
    colors[ImGuiCol_ScrollbarGrabActive]    = rgb(11, 95, 58);
    colors[ImGuiCol_CheckMark]              = rgb(11, 95, 58);
    colors[ImGuiCol_CheckboxSelectedBg]     = rgb(214, 235, 221);
    colors[ImGuiCol_SliderGrab]             = rgb(21, 153, 87);
    colors[ImGuiCol_SliderGrabActive]       = rgb(11, 95, 58);

    colors[ImGuiCol_Button]                 = rgb(226, 237, 228);
    colors[ImGuiCol_ButtonHovered]          = rgb(204, 228, 212);
    colors[ImGuiCol_ButtonActive]           = rgb(178, 214, 190);
    colors[ImGuiCol_Header]                 = rgb(221, 238, 226);
    colors[ImGuiCol_HeaderHovered]          = rgb(204, 228, 212);
    colors[ImGuiCol_HeaderActive]           = rgb(178, 214, 190);

    colors[ImGuiCol_Separator]              = rgb(16, 33, 28, 0.16F);
    colors[ImGuiCol_SeparatorHovered]       = rgb(21, 153, 87, 0.70F);
    colors[ImGuiCol_SeparatorActive]        = rgb(11, 95, 58);
    colors[ImGuiCol_ResizeGrip]             = rgb(21, 153, 87, 0.20F);
    colors[ImGuiCol_ResizeGripHovered]      = rgb(21, 153, 87, 0.65F);
    colors[ImGuiCol_ResizeGripActive]       = rgb(11, 95, 58);
    colors[ImGuiCol_InputTextCursor]        = rgb(11, 95, 58);

    colors[ImGuiCol_Tab]                    = rgb(236, 234, 221);
    colors[ImGuiCol_TabHovered]             = rgb(204, 228, 212);
    colors[ImGuiCol_TabSelected]            = rgb(221, 238, 226);
    colors[ImGuiCol_TabSelectedOverline]    = rgb(21, 153, 87);
    colors[ImGuiCol_TabDimmed]              = rgb(240, 238, 228);
    colors[ImGuiCol_TabDimmedSelected]      = rgb(226, 237, 228);
    colors[ImGuiCol_TabDimmedSelectedOverline] = rgb(11, 95, 58, 0.65F);

    colors[ImGuiCol_PlotLines]              = rgb(21, 153, 87);
    colors[ImGuiCol_PlotLinesHovered]       = rgb(11, 95, 58);
    colors[ImGuiCol_PlotHistogram]          = rgb(242, 169, 59);
    colors[ImGuiCol_PlotHistogramHovered]   = rgb(180, 83, 9);
    colors[ImGuiCol_TableHeaderBg]          = rgb(236, 234, 221);
    colors[ImGuiCol_TableBorderStrong]      = rgb(197, 200, 191);
    colors[ImGuiCol_TableBorderLight]       = rgb(218, 219, 210);
    colors[ImGuiCol_TableRowBg]             = rgb(255, 254, 248, 0.45F);
    colors[ImGuiCol_TableRowBgAlt]          = rgb(236, 234, 221, 0.55F);

    colors[ImGuiCol_TextLink]               = rgb(11, 95, 58);
    colors[ImGuiCol_TextSelectedBg]         = rgb(21, 153, 87, 0.30F);
    colors[ImGuiCol_TreeLines]              = rgb(95, 112, 106);
    colors[ImGuiCol_DragDropTarget]         = rgb(242, 169, 59);
    colors[ImGuiCol_DragDropTargetBg]       = rgb(242, 169, 59, 0.16F);
    colors[ImGuiCol_UnsavedMarker]          = rgb(242, 169, 59);
    colors[ImGuiCol_NavCursor]              = rgb(242, 169, 59);
    colors[ImGuiCol_NavWindowingHighlight]  = rgb(21, 153, 87, 0.75F);
    colors[ImGuiCol_NavWindowingDimBg]      = rgb(7, 24, 19, 0.18F);
    colors[ImGuiCol_ModalWindowDimBg]       = rgb(7, 24, 19, 0.28F);
}

// ─── Dark (neutral đen xám) ─────────────────────────────────────────────────
// Palette: nền đen xám trung tính, accent xanh dương nhạt (slate blue).

void applyDarkTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);
    applyCommonLayout(style);

    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg]               = rgb(18, 18, 22);
    c[ImGuiCol_ChildBg]                = rgb(18, 18, 22);
    c[ImGuiCol_PopupBg]                = rgb(26, 26, 32);
    c[ImGuiCol_MenuBarBg]              = rgb(18, 18, 22);

    c[ImGuiCol_Text]                   = rgb(220, 222, 233);
    c[ImGuiCol_TextDisabled]           = rgb(100, 102, 115);
    c[ImGuiCol_TextLink]               = rgb(130, 170, 255);

    c[ImGuiCol_Border]                 = rgb(255, 255, 255, 0.10F);
    c[ImGuiCol_BorderShadow]           = rgb(0, 0, 0, 0.0F);

    c[ImGuiCol_FrameBg]                = rgb(30, 31, 38);
    c[ImGuiCol_FrameBgHovered]         = rgb(40, 42, 52);
    c[ImGuiCol_FrameBgActive]          = rgb(50, 53, 65);

    c[ImGuiCol_TitleBg]                = rgb(12, 12, 16);
    c[ImGuiCol_TitleBgActive]          = rgb(30, 31, 38);
    c[ImGuiCol_TitleBgCollapsed]       = rgb(12, 12, 16);

    c[ImGuiCol_ScrollbarBg]            = rgb(12, 12, 16, 0.80F);
    c[ImGuiCol_ScrollbarGrab]          = rgb(65, 68, 82);
    c[ImGuiCol_ScrollbarGrabHovered]   = rgb(100, 104, 122);
    c[ImGuiCol_ScrollbarGrabActive]    = rgb(130, 170, 255);

    c[ImGuiCol_CheckMark]              = rgb(130, 170, 255);
    c[ImGuiCol_CheckboxSelectedBg]     = rgb(30, 45, 80);
    c[ImGuiCol_SliderGrab]             = rgb(130, 170, 255);
    c[ImGuiCol_SliderGrabActive]       = rgb(100, 140, 230);
    c[ImGuiCol_InputTextCursor]        = rgb(130, 170, 255);

    c[ImGuiCol_Button]                 = rgb(38, 40, 50);
    c[ImGuiCol_ButtonHovered]          = rgb(52, 55, 68);
    c[ImGuiCol_ButtonActive]           = rgb(65, 68, 85);

    c[ImGuiCol_Header]                 = rgb(38, 40, 50);
    c[ImGuiCol_HeaderHovered]          = rgb(52, 55, 68);
    c[ImGuiCol_HeaderActive]           = rgb(130, 170, 255, 0.35F);

    c[ImGuiCol_Separator]              = rgb(255, 255, 255, 0.10F);
    c[ImGuiCol_SeparatorHovered]       = rgb(130, 170, 255, 0.60F);
    c[ImGuiCol_SeparatorActive]        = rgb(130, 170, 255);

    c[ImGuiCol_ResizeGrip]             = rgb(130, 170, 255, 0.20F);
    c[ImGuiCol_ResizeGripHovered]      = rgb(130, 170, 255, 0.60F);
    c[ImGuiCol_ResizeGripActive]       = rgb(130, 170, 255);

    c[ImGuiCol_Tab]                    = rgb(26, 26, 32);
    c[ImGuiCol_TabHovered]             = rgb(50, 55, 70);
    c[ImGuiCol_TabSelected]            = rgb(38, 40, 50);
    c[ImGuiCol_TabSelectedOverline]    = rgb(130, 170, 255);
    c[ImGuiCol_TabDimmed]              = rgb(20, 20, 26);
    c[ImGuiCol_TabDimmedSelected]      = rgb(28, 29, 36);
    c[ImGuiCol_TabDimmedSelectedOverline] = rgb(100, 130, 200, 0.65F);

    c[ImGuiCol_PlotLines]              = rgb(130, 170, 255);
    c[ImGuiCol_PlotLinesHovered]       = rgb(100, 140, 230);
    c[ImGuiCol_PlotHistogram]          = rgb(255, 200, 100);
    c[ImGuiCol_PlotHistogramHovered]   = rgb(230, 160, 60);

    c[ImGuiCol_TableHeaderBg]          = rgb(26, 26, 32);
    c[ImGuiCol_TableBorderStrong]      = rgb(65, 68, 82);
    c[ImGuiCol_TableBorderLight]       = rgb(38, 40, 50);
    c[ImGuiCol_TableRowBg]             = rgb(0, 0, 0, 0.0F);
    c[ImGuiCol_TableRowBgAlt]          = rgb(255, 255, 255, 0.04F);

    c[ImGuiCol_TextSelectedBg]         = rgb(130, 170, 255, 0.28F);
    c[ImGuiCol_TreeLines]              = rgb(100, 102, 115);
    c[ImGuiCol_DragDropTarget]         = rgb(255, 200, 100);
    c[ImGuiCol_DragDropTargetBg]       = rgb(255, 200, 100, 0.14F);
    c[ImGuiCol_UnsavedMarker]          = rgb(255, 200, 100);
    c[ImGuiCol_NavCursor]              = rgb(255, 200, 100);
    c[ImGuiCol_NavWindowingHighlight]  = rgb(130, 170, 255, 0.70F);
    c[ImGuiCol_NavWindowingDimBg]      = rgb(0, 0, 0, 0.30F);
    c[ImGuiCol_ModalWindowDimBg]       = rgb(0, 0, 0, 0.45F);
}

// ─── Areca Dark ──────────────────────────────────────────────────────────────
// Palette: nền charcoal xanh lá đậm, accent xanh lá + cam.

void applyArecaDarkTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);
    applyCommonLayout(style);

    ImVec4* c = style.Colors;
    // Nền
    c[ImGuiCol_WindowBg]               = rgb(18, 26, 23);   // --bg-deep
    c[ImGuiCol_ChildBg]                = rgb(18, 26, 23);
    c[ImGuiCol_PopupBg]                = rgb(23, 33, 29);
    c[ImGuiCol_MenuBarBg]              = rgb(18, 26, 23);

    // Text
    c[ImGuiCol_Text]                   = rgb(210, 228, 219); // --fg
    c[ImGuiCol_TextDisabled]           = rgb(89, 109, 99);
    c[ImGuiCol_TextLink]               = rgb(56, 200, 120);

    // Border
    c[ImGuiCol_Border]                 = rgb(56, 200, 120, 0.18F);
    c[ImGuiCol_BorderShadow]           = rgb(0, 0, 0, 0.0F);

    // Frame
    c[ImGuiCol_FrameBg]                = rgb(28, 39, 34);
    c[ImGuiCol_FrameBgHovered]         = rgb(36, 52, 44);
    c[ImGuiCol_FrameBgActive]          = rgb(44, 65, 54);

    // Title
    c[ImGuiCol_TitleBg]                = rgb(14, 21, 18);
    c[ImGuiCol_TitleBgActive]          = rgb(22, 42, 32);
    c[ImGuiCol_TitleBgCollapsed]       = rgb(14, 21, 18);

    // Scrollbar
    c[ImGuiCol_ScrollbarBg]            = rgb(14, 21, 18, 0.80F);
    c[ImGuiCol_ScrollbarGrab]          = rgb(56, 86, 70);
    c[ImGuiCol_ScrollbarGrabHovered]   = rgb(56, 140, 90);
    c[ImGuiCol_ScrollbarGrabActive]    = rgb(21, 153, 87);

    // Check / Slider
    c[ImGuiCol_CheckMark]              = rgb(56, 200, 120);
    c[ImGuiCol_CheckboxSelectedBg]     = rgb(21, 60, 40);
    c[ImGuiCol_SliderGrab]             = rgb(56, 200, 120);
    c[ImGuiCol_SliderGrabActive]       = rgb(21, 153, 87);
    c[ImGuiCol_InputTextCursor]        = rgb(56, 200, 120);

    // Button
    c[ImGuiCol_Button]                 = rgb(30, 56, 42);
    c[ImGuiCol_ButtonHovered]          = rgb(38, 72, 54);
    c[ImGuiCol_ButtonActive]           = rgb(21, 90, 55);

    // Header (selectable, combo item)
    c[ImGuiCol_Header]                 = rgb(28, 60, 44);
    c[ImGuiCol_HeaderHovered]          = rgb(38, 72, 54);
    c[ImGuiCol_HeaderActive]           = rgb(21, 100, 60);

    // Separator
    c[ImGuiCol_Separator]              = rgb(56, 200, 120, 0.20F);
    c[ImGuiCol_SeparatorHovered]       = rgb(56, 200, 120, 0.60F);
    c[ImGuiCol_SeparatorActive]        = rgb(56, 200, 120);

    // Resize
    c[ImGuiCol_ResizeGrip]             = rgb(56, 200, 120, 0.20F);
    c[ImGuiCol_ResizeGripHovered]      = rgb(56, 200, 120, 0.60F);
    c[ImGuiCol_ResizeGripActive]       = rgb(56, 200, 120);

    // Tab
    c[ImGuiCol_Tab]                    = rgb(22, 34, 28);
    c[ImGuiCol_TabHovered]             = rgb(36, 72, 52);
    c[ImGuiCol_TabSelected]            = rgb(28, 50, 38);
    c[ImGuiCol_TabSelectedOverline]    = rgb(56, 200, 120);
    c[ImGuiCol_TabDimmed]              = rgb(18, 28, 23);
    c[ImGuiCol_TabDimmedSelected]      = rgb(22, 38, 30);
    c[ImGuiCol_TabDimmedSelectedOverline] = rgb(21, 120, 70, 0.70F);

    // Plot
    c[ImGuiCol_PlotLines]              = rgb(56, 200, 120);
    c[ImGuiCol_PlotLinesHovered]       = rgb(21, 153, 87);
    c[ImGuiCol_PlotHistogram]          = rgb(242, 169, 59);
    c[ImGuiCol_PlotHistogramHovered]   = rgb(220, 130, 30);

    // Table
    c[ImGuiCol_TableHeaderBg]          = rgb(22, 34, 28);
    c[ImGuiCol_TableBorderStrong]      = rgb(56, 80, 66);
    c[ImGuiCol_TableBorderLight]       = rgb(36, 52, 44);
    c[ImGuiCol_TableRowBg]             = rgb(0, 0, 0, 0.0F);
    c[ImGuiCol_TableRowBgAlt]          = rgb(255, 255, 255, 0.04F);

    // Misc
    c[ImGuiCol_TextSelectedBg]         = rgb(56, 200, 120, 0.28F);
    c[ImGuiCol_TreeLines]              = rgb(89, 109, 99);
    c[ImGuiCol_DragDropTarget]         = rgb(242, 169, 59);
    c[ImGuiCol_DragDropTargetBg]       = rgb(242, 169, 59, 0.14F);
    c[ImGuiCol_UnsavedMarker]          = rgb(242, 169, 59);
    c[ImGuiCol_NavCursor]              = rgb(242, 169, 59);
    c[ImGuiCol_NavWindowingHighlight]  = rgb(56, 200, 120, 0.70F);
    c[ImGuiCol_NavWindowingDimBg]      = rgb(0, 0, 0, 0.30F);
    c[ImGuiCol_ModalWindowDimBg]       = rgb(0, 0, 0, 0.40F);
}

// ─── Gruvbox Dark ───────────────────────────────────────────────────────────
// Palette chính thức: https://github.com/morhetz/gruvbox

void applyGruvboxTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);
    applyCommonLayout(style);

    // Gruvbox Dark palette
    // bg:  #282828  bg1: #3c3836  bg2: #504945  bg3: #665c54
    // fg:  #ebdbb2  fg1: #d5c4a1  fg2: #bdae93  fg3: #a89984
    // red: #cc241d  orange: #d65d0e  yellow: #d79921
    // green: #98971a  aqua: #689d6a  blue: #458588  purple: #b16286

    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg]               = rgb(40, 40, 40);    // bg
    c[ImGuiCol_ChildBg]                = rgb(40, 40, 40);
    c[ImGuiCol_PopupBg]                = rgb(50, 48, 47);    // bg1
    c[ImGuiCol_MenuBarBg]              = rgb(40, 40, 40);

    c[ImGuiCol_Text]                   = rgb(235, 219, 178); // fg
    c[ImGuiCol_TextDisabled]           = rgb(168, 153, 132); // fg3
    c[ImGuiCol_TextLink]               = rgb(104, 157, 106); // aqua

    c[ImGuiCol_Border]                 = rgb(168, 153, 132, 0.30F);
    c[ImGuiCol_BorderShadow]           = rgb(0, 0, 0, 0.0F);

    c[ImGuiCol_FrameBg]                = rgb(60, 56, 54);    // bg1
    c[ImGuiCol_FrameBgHovered]         = rgb(80, 73, 69);    // bg2
    c[ImGuiCol_FrameBgActive]          = rgb(102, 92, 84);   // bg3

    c[ImGuiCol_TitleBg]                = rgb(29, 32, 33);    // bg hard
    c[ImGuiCol_TitleBgActive]          = rgb(60, 56, 54);
    c[ImGuiCol_TitleBgCollapsed]       = rgb(29, 32, 33);

    c[ImGuiCol_ScrollbarBg]            = rgb(29, 32, 33, 0.80F);
    c[ImGuiCol_ScrollbarGrab]          = rgb(102, 92, 84);
    c[ImGuiCol_ScrollbarGrabHovered]   = rgb(168, 153, 132);
    c[ImGuiCol_ScrollbarGrabActive]    = rgb(213, 196, 161); // fg1

    c[ImGuiCol_CheckMark]              = rgb(215, 153, 33);   // yellow (accent)
    c[ImGuiCol_CheckboxSelectedBg]     = rgb(71, 55, 20);
    c[ImGuiCol_SliderGrab]             = rgb(215, 153, 33);  // yellow
    c[ImGuiCol_SliderGrabActive]       = rgb(250, 189, 47);
    c[ImGuiCol_InputTextCursor]        = rgb(215, 153, 33);

    c[ImGuiCol_Button]                 = rgb(80, 73, 69);
    c[ImGuiCol_ButtonHovered]          = rgb(102, 92, 84);
    c[ImGuiCol_ButtonActive]           = rgb(124, 111, 100);

    c[ImGuiCol_Header]                 = rgb(80, 73, 69);
    c[ImGuiCol_HeaderHovered]          = rgb(102, 92, 84);
    c[ImGuiCol_HeaderActive]           = rgb(215, 153, 33, 0.40F);

    c[ImGuiCol_Separator]              = rgb(168, 153, 132, 0.25F);
    c[ImGuiCol_SeparatorHovered]       = rgb(215, 153, 33, 0.70F);
    c[ImGuiCol_SeparatorActive]        = rgb(215, 153, 33);

    c[ImGuiCol_ResizeGrip]             = rgb(215, 153, 33, 0.20F);
    c[ImGuiCol_ResizeGripHovered]      = rgb(215, 153, 33, 0.60F);
    c[ImGuiCol_ResizeGripActive]       = rgb(215, 153, 33);

    c[ImGuiCol_Tab]                    = rgb(60, 56, 54);
    c[ImGuiCol_TabHovered]             = rgb(102, 92, 84);
    c[ImGuiCol_TabSelected]            = rgb(80, 73, 69);
    c[ImGuiCol_TabSelectedOverline]    = rgb(215, 153, 33);
    c[ImGuiCol_TabDimmed]              = rgb(50, 48, 47);
    c[ImGuiCol_TabDimmedSelected]      = rgb(60, 56, 54);
    c[ImGuiCol_TabDimmedSelectedOverline] = rgb(152, 151, 26, 0.60F);

    c[ImGuiCol_PlotLines]              = rgb(104, 157, 106); // aqua
    c[ImGuiCol_PlotLinesHovered]       = rgb(69, 133, 136);  // blue
    c[ImGuiCol_PlotHistogram]          = rgb(215, 153, 33);
    c[ImGuiCol_PlotHistogramHovered]   = rgb(214, 93, 14);   // orange

    c[ImGuiCol_TableHeaderBg]          = rgb(60, 56, 54);
    c[ImGuiCol_TableBorderStrong]      = rgb(102, 92, 84);
    c[ImGuiCol_TableBorderLight]       = rgb(80, 73, 69);
    c[ImGuiCol_TableRowBg]             = rgb(0, 0, 0, 0.0F);
    c[ImGuiCol_TableRowBgAlt]          = rgb(255, 255, 255, 0.04F);

    c[ImGuiCol_TextSelectedBg]         = rgb(215, 153, 33, 0.28F);
    c[ImGuiCol_TreeLines]              = rgb(168, 153, 132);
    c[ImGuiCol_DragDropTarget]         = rgb(214, 93, 14);
    c[ImGuiCol_DragDropTargetBg]       = rgb(214, 93, 14, 0.16F);
    c[ImGuiCol_UnsavedMarker]          = rgb(214, 93, 14);
    c[ImGuiCol_NavCursor]              = rgb(215, 153, 33);
    c[ImGuiCol_NavWindowingHighlight]  = rgb(215, 153, 33, 0.70F);
    c[ImGuiCol_NavWindowingDimBg]      = rgb(0, 0, 0, 0.30F);
    c[ImGuiCol_ModalWindowDimBg]       = rgb(0, 0, 0, 0.40F);
}

// ─── Catppuccin Mocha ───────────────────────────────────────────────────────
// Palette chính thức: https://github.com/catppuccin/catppuccin

void applyCatppuccinTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);
    applyCommonLayout(style);

    // Catppuccin Mocha palette
    // Base:    #1e1e2e  Mantle: #181825  Crust:   #11111b
    // Surface0:#313244  Surface1:#45475a  Surface2:#585b70
    // Overlay0:#6c7086  Overlay1:#7f849c  Overlay2:#9399b2
    // Subtext0:#a6adc8  Subtext1:#bac2de  Text:    #cdd6f4
    // Lavender:#b4befe  Blue:    #89b4fa  Sapphire:#74c7ec
    // Sky:     #89dceb  Teal:    #94e2d5  Green:   #a6e3a1
    // Yellow:  #f9e2af  Peach:   #fab387  Maroon:  #eba0ac
    // Red:     #f38ba8  Mauve:   #cba6f7  Pink:    #f5c2e7
    // Flamingo:#f2cdcd  Rosewater:#f5e0dc

    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg]               = rgb(30, 30, 46);    // Base
    c[ImGuiCol_ChildBg]                = rgb(30, 30, 46);
    c[ImGuiCol_PopupBg]                = rgb(24, 24, 37);    // Mantle
    c[ImGuiCol_MenuBarBg]              = rgb(30, 30, 46);

    c[ImGuiCol_Text]                   = rgb(205, 214, 244); // Text
    c[ImGuiCol_TextDisabled]           = rgb(108, 112, 134); // Overlay0
    c[ImGuiCol_TextLink]               = rgb(137, 180, 250); // Blue

    c[ImGuiCol_Border]                 = rgb(49, 50, 68, 0.80F);   // Surface0
    c[ImGuiCol_BorderShadow]           = rgb(0, 0, 0, 0.0F);

    c[ImGuiCol_FrameBg]                = rgb(49, 50, 68);    // Surface0
    c[ImGuiCol_FrameBgHovered]         = rgb(69, 71, 90);    // Surface1
    c[ImGuiCol_FrameBgActive]          = rgb(88, 91, 112);   // Surface2

    c[ImGuiCol_TitleBg]                = rgb(17, 17, 27);    // Crust
    c[ImGuiCol_TitleBgActive]          = rgb(49, 50, 68);
    c[ImGuiCol_TitleBgCollapsed]       = rgb(17, 17, 27);

    c[ImGuiCol_ScrollbarBg]            = rgb(17, 17, 27, 0.80F);
    c[ImGuiCol_ScrollbarGrab]          = rgb(88, 91, 112);   // Surface2
    c[ImGuiCol_ScrollbarGrabHovered]   = rgb(166, 173, 200); // Subtext0
    c[ImGuiCol_ScrollbarGrabActive]    = rgb(203, 166, 247); // Mauve

    c[ImGuiCol_CheckMark]              = rgb(203, 166, 247); // Mauve (accent)
    c[ImGuiCol_CheckboxSelectedBg]     = rgb(55, 45, 75);
    c[ImGuiCol_SliderGrab]             = rgb(203, 166, 247); // Mauve
    c[ImGuiCol_SliderGrabActive]       = rgb(180, 144, 240);
    c[ImGuiCol_InputTextCursor]        = rgb(203, 166, 247);

    c[ImGuiCol_Button]                 = rgb(49, 50, 68);    // Surface0
    c[ImGuiCol_ButtonHovered]          = rgb(69, 71, 90);    // Surface1
    c[ImGuiCol_ButtonActive]           = rgb(88, 91, 112);   // Surface2

    c[ImGuiCol_Header]                 = rgb(49, 50, 68);
    c[ImGuiCol_HeaderHovered]          = rgb(69, 71, 90);
    c[ImGuiCol_HeaderActive]           = rgb(203, 166, 247, 0.40F);

    c[ImGuiCol_Separator]              = rgb(69, 71, 90, 0.60F);
    c[ImGuiCol_SeparatorHovered]       = rgb(203, 166, 247, 0.70F);
    c[ImGuiCol_SeparatorActive]        = rgb(203, 166, 247);

    c[ImGuiCol_ResizeGrip]             = rgb(203, 166, 247, 0.20F);
    c[ImGuiCol_ResizeGripHovered]      = rgb(203, 166, 247, 0.60F);
    c[ImGuiCol_ResizeGripActive]       = rgb(203, 166, 247);

    c[ImGuiCol_Tab]                    = rgb(49, 50, 68);
    c[ImGuiCol_TabHovered]             = rgb(69, 71, 90);
    c[ImGuiCol_TabSelected]            = rgb(49, 50, 68);
    c[ImGuiCol_TabSelectedOverline]    = rgb(203, 166, 247); // Mauve
    c[ImGuiCol_TabDimmed]              = rgb(30, 30, 46);
    c[ImGuiCol_TabDimmedSelected]      = rgb(39, 39, 57);
    c[ImGuiCol_TabDimmedSelectedOverline] = rgb(137, 180, 250, 0.60F);

    c[ImGuiCol_PlotLines]              = rgb(166, 227, 161); // Green
    c[ImGuiCol_PlotLinesHovered]       = rgb(148, 226, 213); // Teal
    c[ImGuiCol_PlotHistogram]          = rgb(249, 226, 175); // Yellow
    c[ImGuiCol_PlotHistogramHovered]   = rgb(250, 179, 135); // Peach

    c[ImGuiCol_TableHeaderBg]          = rgb(49, 50, 68);
    c[ImGuiCol_TableBorderStrong]      = rgb(88, 91, 112);
    c[ImGuiCol_TableBorderLight]       = rgb(69, 71, 90);
    c[ImGuiCol_TableRowBg]             = rgb(0, 0, 0, 0.0F);
    c[ImGuiCol_TableRowBgAlt]          = rgb(255, 255, 255, 0.04F);

    c[ImGuiCol_TextSelectedBg]         = rgb(203, 166, 247, 0.28F);
    c[ImGuiCol_TreeLines]              = rgb(108, 112, 134); // Overlay0
    c[ImGuiCol_DragDropTarget]         = rgb(249, 226, 175); // Yellow
    c[ImGuiCol_DragDropTargetBg]       = rgb(249, 226, 175, 0.14F);
    c[ImGuiCol_UnsavedMarker]          = rgb(249, 226, 175);
    c[ImGuiCol_NavCursor]              = rgb(203, 166, 247);
    c[ImGuiCol_NavWindowingHighlight]  = rgb(203, 166, 247, 0.70F);
    c[ImGuiCol_NavWindowingDimBg]      = rgb(0, 0, 0, 0.30F);
    c[ImGuiCol_ModalWindowDimBg]       = rgb(0, 0, 0, 0.40F);
}


// ─── Catppuccin Latte ───────────────────────────────────────────────────────
// Palette chính thức: https://github.com/catppuccin/catppuccin (light variant)

void applyCatppuccinLatteTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsLight(&style);
    applyCommonLayout(style);

    // Latte palette
    // Base:#eff1f5  Mantle:#e6e9ef  Crust:#dce0e8
    // Surface0:#ccd0da  Surface1:#bcc0cc  Surface2:#acb0be
    // Text:#4c4f69  Subtext0:#6c6f85  Overlay0:#9ca0b0
    // Blue:#1e66f5  Mauve:#8839ef  Green:#40a02b  Yellow:#df8e1d  Peach:#fe640b  Red:#d20f39

    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg]               = rgb(239, 241, 245);  // Base
    c[ImGuiCol_ChildBg]                = rgb(239, 241, 245);
    c[ImGuiCol_PopupBg]                = rgb(230, 233, 239);  // Mantle
    c[ImGuiCol_MenuBarBg]              = rgb(239, 241, 245);

    c[ImGuiCol_Text]                   = rgb(76, 79, 105);    // Text
    c[ImGuiCol_TextDisabled]           = rgb(156, 160, 176);  // Overlay0
    c[ImGuiCol_TextLink]               = rgb(30, 102, 245);   // Blue

    c[ImGuiCol_Border]                 = rgb(76, 79, 105, 0.20F);
    c[ImGuiCol_BorderShadow]           = rgb(0, 0, 0, 0.0F);

    c[ImGuiCol_FrameBg]                = rgb(204, 208, 218);  // Surface0
    c[ImGuiCol_FrameBgHovered]         = rgb(188, 192, 204);  // Surface1
    c[ImGuiCol_FrameBgActive]          = rgb(172, 176, 190);  // Surface2

    c[ImGuiCol_TitleBg]                = rgb(220, 224, 232);  // Crust
    c[ImGuiCol_TitleBgActive]          = rgb(204, 208, 218);
    c[ImGuiCol_TitleBgCollapsed]       = rgb(220, 224, 232);

    c[ImGuiCol_ScrollbarBg]            = rgb(220, 224, 232, 0.80F);
    c[ImGuiCol_ScrollbarGrab]          = rgb(172, 176, 190);
    c[ImGuiCol_ScrollbarGrabHovered]   = rgb(108, 111, 133);
    c[ImGuiCol_ScrollbarGrabActive]    = rgb(136, 57, 239);   // Mauve

    c[ImGuiCol_CheckMark]              = rgb(136, 57, 239);   // Mauve (accent)
    c[ImGuiCol_CheckboxSelectedBg]     = rgb(230, 215, 250);
    c[ImGuiCol_SliderGrab]             = rgb(136, 57, 239);   // Mauve
    c[ImGuiCol_SliderGrabActive]       = rgb(100, 30, 200);
    c[ImGuiCol_InputTextCursor]        = rgb(136, 57, 239);

    c[ImGuiCol_Button]                 = rgb(204, 208, 218);
    c[ImGuiCol_ButtonHovered]          = rgb(188, 192, 204);
    c[ImGuiCol_ButtonActive]           = rgb(172, 176, 190);

    c[ImGuiCol_Header]                 = rgb(204, 208, 218);
    c[ImGuiCol_HeaderHovered]          = rgb(188, 192, 204);
    c[ImGuiCol_HeaderActive]           = rgb(136, 57, 239, 0.35F);

    c[ImGuiCol_Separator]              = rgb(76, 79, 105, 0.20F);
    c[ImGuiCol_SeparatorHovered]       = rgb(136, 57, 239, 0.60F);
    c[ImGuiCol_SeparatorActive]        = rgb(136, 57, 239);

    c[ImGuiCol_ResizeGrip]             = rgb(136, 57, 239, 0.20F);
    c[ImGuiCol_ResizeGripHovered]      = rgb(136, 57, 239, 0.60F);
    c[ImGuiCol_ResizeGripActive]       = rgb(136, 57, 239);

    c[ImGuiCol_Tab]                    = rgb(220, 224, 232);
    c[ImGuiCol_TabHovered]             = rgb(188, 192, 204);
    c[ImGuiCol_TabSelected]            = rgb(204, 208, 218);
    c[ImGuiCol_TabSelectedOverline]    = rgb(136, 57, 239);
    c[ImGuiCol_TabDimmed]              = rgb(230, 233, 239);
    c[ImGuiCol_TabDimmedSelected]      = rgb(212, 216, 228);
    c[ImGuiCol_TabDimmedSelectedOverline] = rgb(100, 30, 200, 0.60F);

    c[ImGuiCol_PlotLines]              = rgb(64, 160, 43);
    c[ImGuiCol_PlotLinesHovered]       = rgb(25, 146, 185);
    c[ImGuiCol_PlotHistogram]          = rgb(223, 142, 29);   // Yellow
    c[ImGuiCol_PlotHistogramHovered]   = rgb(254, 100, 11);   // Peach

    c[ImGuiCol_TableHeaderBg]          = rgb(220, 224, 232);
    c[ImGuiCol_TableBorderStrong]      = rgb(172, 176, 190);
    c[ImGuiCol_TableBorderLight]       = rgb(188, 192, 204);
    c[ImGuiCol_TableRowBg]             = rgb(0, 0, 0, 0.0F);
    c[ImGuiCol_TableRowBgAlt]          = rgb(0, 0, 0, 0.04F);

    c[ImGuiCol_TextSelectedBg]         = rgb(136, 57, 239, 0.28F);
    c[ImGuiCol_TreeLines]              = rgb(156, 160, 176);
    c[ImGuiCol_DragDropTarget]         = rgb(223, 142, 29);
    c[ImGuiCol_DragDropTargetBg]       = rgb(223, 142, 29, 0.14F);
    c[ImGuiCol_UnsavedMarker]          = rgb(254, 100, 11);   // Peach
    c[ImGuiCol_NavCursor]              = rgb(136, 57, 239);
    c[ImGuiCol_NavWindowingHighlight]  = rgb(136, 57, 239, 0.70F);
    c[ImGuiCol_NavWindowingDimBg]      = rgb(76, 79, 105, 0.20F);
    c[ImGuiCol_ModalWindowDimBg]       = rgb(76, 79, 105, 0.30F);
}

// ─── Dracula ────────────────────────────────────────────────────────────────
// Palette chính thức: https://draculatheme.com/contribute

void applyDraculaTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);
    applyCommonLayout(style);

    // bg:#282a36  currentLine:#44475a  fg:#f8f8f2
    // comment:#6272a4  cyan:#8be9fd  green:#50fa7b
    // orange:#ffb86c  pink:#ff79c6  purple:#bd93f9
    // red:#ff5555  yellow:#f1fa8c

    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg]               = rgb(40, 42, 54);     // bg
    c[ImGuiCol_ChildBg]                = rgb(40, 42, 54);
    c[ImGuiCol_PopupBg]                = rgb(33, 34, 44);
    c[ImGuiCol_MenuBarBg]              = rgb(40, 42, 54);

    c[ImGuiCol_Text]                   = rgb(248, 248, 242);  // fg
    c[ImGuiCol_TextDisabled]           = rgb(98, 114, 164);   // comment
    c[ImGuiCol_TextLink]               = rgb(139, 233, 253);  // cyan

    c[ImGuiCol_Border]                 = rgb(68, 71, 90, 0.80F);   // currentLine
    c[ImGuiCol_BorderShadow]           = rgb(0, 0, 0, 0.0F);

    c[ImGuiCol_FrameBg]                = rgb(68, 71, 90);     // currentLine
    c[ImGuiCol_FrameBgHovered]         = rgb(82, 85, 108);
    c[ImGuiCol_FrameBgActive]          = rgb(98, 102, 130);

    c[ImGuiCol_TitleBg]                = rgb(24, 25, 33);
    c[ImGuiCol_TitleBgActive]          = rgb(68, 71, 90);
    c[ImGuiCol_TitleBgCollapsed]       = rgb(24, 25, 33);

    c[ImGuiCol_ScrollbarBg]            = rgb(24, 25, 33, 0.80F);
    c[ImGuiCol_ScrollbarGrab]          = rgb(98, 114, 164);
    c[ImGuiCol_ScrollbarGrabHovered]   = rgb(139, 233, 253);
    c[ImGuiCol_ScrollbarGrabActive]    = rgb(189, 147, 249);  // purple

    c[ImGuiCol_CheckMark]              = rgb(189, 147, 249);  // purple (accent)
    c[ImGuiCol_CheckboxSelectedBg]     = rgb(65, 50, 90);
    c[ImGuiCol_SliderGrab]             = rgb(189, 147, 249);  // purple
    c[ImGuiCol_SliderGrabActive]       = rgb(160, 110, 230);
    c[ImGuiCol_InputTextCursor]        = rgb(189, 147, 249);

    c[ImGuiCol_Button]                 = rgb(68, 71, 90);
    c[ImGuiCol_ButtonHovered]          = rgb(82, 85, 108);
    c[ImGuiCol_ButtonActive]           = rgb(98, 102, 130);

    c[ImGuiCol_Header]                 = rgb(68, 71, 90);
    c[ImGuiCol_HeaderHovered]          = rgb(82, 85, 108);
    c[ImGuiCol_HeaderActive]           = rgb(189, 147, 249, 0.40F);

    c[ImGuiCol_Separator]              = rgb(68, 71, 90, 0.80F);
    c[ImGuiCol_SeparatorHovered]       = rgb(189, 147, 249, 0.60F);
    c[ImGuiCol_SeparatorActive]        = rgb(189, 147, 249);

    c[ImGuiCol_ResizeGrip]             = rgb(189, 147, 249, 0.20F);
    c[ImGuiCol_ResizeGripHovered]      = rgb(189, 147, 249, 0.60F);
    c[ImGuiCol_ResizeGripActive]       = rgb(189, 147, 249);

    c[ImGuiCol_Tab]                    = rgb(40, 42, 54);
    c[ImGuiCol_TabHovered]             = rgb(82, 85, 108);
    c[ImGuiCol_TabSelected]            = rgb(68, 71, 90);
    c[ImGuiCol_TabSelectedOverline]    = rgb(189, 147, 249);
    c[ImGuiCol_TabDimmed]              = rgb(33, 34, 44);
    c[ImGuiCol_TabDimmedSelected]      = rgb(50, 52, 68);
    c[ImGuiCol_TabDimmedSelectedOverline] = rgb(139, 233, 253, 0.60F);

    c[ImGuiCol_PlotLines]              = rgb(80, 250, 123);
    c[ImGuiCol_PlotLinesHovered]       = rgb(139, 233, 253);
    c[ImGuiCol_PlotHistogram]          = rgb(241, 250, 140);  // yellow
    c[ImGuiCol_PlotHistogramHovered]   = rgb(255, 184, 108);  // orange

    c[ImGuiCol_TableHeaderBg]          = rgb(68, 71, 90);
    c[ImGuiCol_TableBorderStrong]      = rgb(98, 114, 164);
    c[ImGuiCol_TableBorderLight]       = rgb(82, 85, 108);
    c[ImGuiCol_TableRowBg]             = rgb(0, 0, 0, 0.0F);
    c[ImGuiCol_TableRowBgAlt]          = rgb(255, 255, 255, 0.04F);

    c[ImGuiCol_TextSelectedBg]         = rgb(189, 147, 249, 0.28F);
    c[ImGuiCol_TreeLines]              = rgb(98, 114, 164);
    c[ImGuiCol_DragDropTarget]         = rgb(241, 250, 140);
    c[ImGuiCol_DragDropTargetBg]       = rgb(241, 250, 140, 0.14F);
    c[ImGuiCol_UnsavedMarker]          = rgb(255, 184, 108);  // orange
    c[ImGuiCol_NavCursor]              = rgb(189, 147, 249);
    c[ImGuiCol_NavWindowingHighlight]  = rgb(189, 147, 249, 0.70F);
    c[ImGuiCol_NavWindowingDimBg]      = rgb(0, 0, 0, 0.30F);
    c[ImGuiCol_ModalWindowDimBg]       = rgb(0, 0, 0, 0.45F);
}

// ─── One Dark ───────────────────────────────────────────────────────────────
// Palette: Atom One Dark

void applyOneDarkTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);
    applyCommonLayout(style);

    // bg:#282c34  bg1:#2c313c  bg2:#3e4452
    // fg:#abb2bf  comment:#5c6370
    // red:#e06c75  orange:#d19a66  yellow:#e5c07b
    // green:#98c379  cyan:#56b6c2  blue:#61afef  purple:#c678dd

    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg]               = rgb(40, 44, 52);     // bg
    c[ImGuiCol_ChildBg]                = rgb(40, 44, 52);
    c[ImGuiCol_PopupBg]                = rgb(33, 37, 43);
    c[ImGuiCol_MenuBarBg]              = rgb(40, 44, 52);

    c[ImGuiCol_Text]                   = rgb(171, 178, 191);  // fg
    c[ImGuiCol_TextDisabled]           = rgb(92, 99, 112);    // comment
    c[ImGuiCol_TextLink]               = rgb(97, 175, 239);   // blue

    c[ImGuiCol_Border]                 = rgb(255, 255, 255, 0.08F);
    c[ImGuiCol_BorderShadow]           = rgb(0, 0, 0, 0.0F);

    c[ImGuiCol_FrameBg]                = rgb(44, 49, 60);     // bg1
    c[ImGuiCol_FrameBgHovered]         = rgb(56, 60, 74);
    c[ImGuiCol_FrameBgActive]          = rgb(62, 68, 82);     // bg2

    c[ImGuiCol_TitleBg]                = rgb(24, 27, 33);
    c[ImGuiCol_TitleBgActive]          = rgb(44, 49, 60);
    c[ImGuiCol_TitleBgCollapsed]       = rgb(24, 27, 33);

    c[ImGuiCol_ScrollbarBg]            = rgb(24, 27, 33, 0.80F);
    c[ImGuiCol_ScrollbarGrab]          = rgb(62, 68, 82);
    c[ImGuiCol_ScrollbarGrabHovered]   = rgb(92, 99, 112);
    c[ImGuiCol_ScrollbarGrabActive]    = rgb(97, 175, 239);

    c[ImGuiCol_CheckMark]              = rgb(97, 175, 239);   // blue (accent)
    c[ImGuiCol_CheckboxSelectedBg]     = rgb(35, 55, 75);
    c[ImGuiCol_SliderGrab]             = rgb(97, 175, 239);   // blue
    c[ImGuiCol_SliderGrabActive]       = rgb(70, 140, 200);
    c[ImGuiCol_InputTextCursor]        = rgb(97, 175, 239);

    c[ImGuiCol_Button]                 = rgb(44, 49, 60);
    c[ImGuiCol_ButtonHovered]          = rgb(56, 60, 74);
    c[ImGuiCol_ButtonActive]           = rgb(62, 68, 82);

    c[ImGuiCol_Header]                 = rgb(44, 49, 60);
    c[ImGuiCol_HeaderHovered]          = rgb(56, 60, 74);
    c[ImGuiCol_HeaderActive]           = rgb(97, 175, 239, 0.35F);

    c[ImGuiCol_Separator]              = rgb(255, 255, 255, 0.08F);
    c[ImGuiCol_SeparatorHovered]       = rgb(97, 175, 239, 0.60F);
    c[ImGuiCol_SeparatorActive]        = rgb(97, 175, 239);

    c[ImGuiCol_ResizeGrip]             = rgb(97, 175, 239, 0.20F);
    c[ImGuiCol_ResizeGripHovered]      = rgb(97, 175, 239, 0.60F);
    c[ImGuiCol_ResizeGripActive]       = rgb(97, 175, 239);

    c[ImGuiCol_Tab]                    = rgb(33, 37, 43);
    c[ImGuiCol_TabHovered]             = rgb(56, 60, 74);
    c[ImGuiCol_TabSelected]            = rgb(44, 49, 60);
    c[ImGuiCol_TabSelectedOverline]    = rgb(97, 175, 239);
    c[ImGuiCol_TabDimmed]              = rgb(28, 31, 38);
    c[ImGuiCol_TabDimmedSelected]      = rgb(38, 42, 52);
    c[ImGuiCol_TabDimmedSelectedOverline] = rgb(70, 140, 200, 0.65F);

    c[ImGuiCol_PlotLines]              = rgb(152, 195, 121);
    c[ImGuiCol_PlotLinesHovered]       = rgb(86, 182, 194);   // cyan
    c[ImGuiCol_PlotHistogram]          = rgb(229, 192, 123);  // yellow
    c[ImGuiCol_PlotHistogramHovered]   = rgb(209, 154, 102);  // orange

    c[ImGuiCol_TableHeaderBg]          = rgb(44, 49, 60);
    c[ImGuiCol_TableBorderStrong]      = rgb(62, 68, 82);
    c[ImGuiCol_TableBorderLight]       = rgb(56, 60, 74);
    c[ImGuiCol_TableRowBg]             = rgb(0, 0, 0, 0.0F);
    c[ImGuiCol_TableRowBgAlt]          = rgb(255, 255, 255, 0.04F);

    c[ImGuiCol_TextSelectedBg]         = rgb(97, 175, 239, 0.28F);
    c[ImGuiCol_TreeLines]              = rgb(92, 99, 112);
    c[ImGuiCol_DragDropTarget]         = rgb(229, 192, 123);
    c[ImGuiCol_DragDropTargetBg]       = rgb(229, 192, 123, 0.14F);
    c[ImGuiCol_UnsavedMarker]          = rgb(209, 154, 102);  // orange
    c[ImGuiCol_NavCursor]              = rgb(198, 120, 221);  // purple
    c[ImGuiCol_NavWindowingHighlight]  = rgb(97, 175, 239, 0.70F);
    c[ImGuiCol_NavWindowingDimBg]      = rgb(0, 0, 0, 0.30F);
    c[ImGuiCol_ModalWindowDimBg]       = rgb(0, 0, 0, 0.45F);
}

// ─── Tokyo Night ────────────────────────────────────────────────────────────
// Palette: https://github.com/tokyo-night/tokyo-night-vscode-theme

void applyTokyoNightTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);
    applyCommonLayout(style);

    // bg:#1a1b2e  bgHL:#24283b  fg:#c0caf5
    // comment:#565f89  red:#f7768e  orange:#ff9e64
    // yellow:#e0af68  green:#9ece6a  teal:#73daca
    // cyan:#7dcfff  blue:#7aa2f7  purple:#bb9af7

    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg]               = rgb(26, 27, 46);     // bg
    c[ImGuiCol_ChildBg]                = rgb(26, 27, 46);
    c[ImGuiCol_PopupBg]                = rgb(19, 20, 33);
    c[ImGuiCol_MenuBarBg]              = rgb(26, 27, 46);

    c[ImGuiCol_Text]                   = rgb(192, 202, 245);  // fg
    c[ImGuiCol_TextDisabled]           = rgb(86, 95, 137);    // comment
    c[ImGuiCol_TextLink]               = rgb(122, 162, 247);  // blue

    c[ImGuiCol_Border]                 = rgb(255, 255, 255, 0.08F);
    c[ImGuiCol_BorderShadow]           = rgb(0, 0, 0, 0.0F);

    c[ImGuiCol_FrameBg]                = rgb(36, 40, 59);     // bgHL
    c[ImGuiCol_FrameBgHovered]         = rgb(46, 50, 72);
    c[ImGuiCol_FrameBgActive]          = rgb(56, 60, 85);

    c[ImGuiCol_TitleBg]                = rgb(15, 16, 26);
    c[ImGuiCol_TitleBgActive]          = rgb(36, 40, 59);
    c[ImGuiCol_TitleBgCollapsed]       = rgb(15, 16, 26);

    c[ImGuiCol_ScrollbarBg]            = rgb(15, 16, 26, 0.80F);
    c[ImGuiCol_ScrollbarGrab]          = rgb(56, 62, 90);
    c[ImGuiCol_ScrollbarGrabHovered]   = rgb(86, 95, 137);
    c[ImGuiCol_ScrollbarGrabActive]    = rgb(122, 162, 247);

    c[ImGuiCol_CheckMark]              = rgb(122, 162, 247);  // blue (accent)
    c[ImGuiCol_CheckboxSelectedBg]     = rgb(30, 45, 75);
    c[ImGuiCol_SliderGrab]             = rgb(122, 162, 247);  // blue
    c[ImGuiCol_SliderGrabActive]       = rgb(90, 128, 210);
    c[ImGuiCol_InputTextCursor]        = rgb(122, 162, 247);

    c[ImGuiCol_Button]                 = rgb(36, 40, 59);
    c[ImGuiCol_ButtonHovered]          = rgb(46, 50, 72);
    c[ImGuiCol_ButtonActive]           = rgb(56, 60, 85);

    c[ImGuiCol_Header]                 = rgb(36, 40, 59);
    c[ImGuiCol_HeaderHovered]          = rgb(46, 50, 72);
    c[ImGuiCol_HeaderActive]           = rgb(122, 162, 247, 0.35F);

    c[ImGuiCol_Separator]              = rgb(255, 255, 255, 0.08F);
    c[ImGuiCol_SeparatorHovered]       = rgb(122, 162, 247, 0.60F);
    c[ImGuiCol_SeparatorActive]        = rgb(122, 162, 247);

    c[ImGuiCol_ResizeGrip]             = rgb(122, 162, 247, 0.20F);
    c[ImGuiCol_ResizeGripHovered]      = rgb(122, 162, 247, 0.60F);
    c[ImGuiCol_ResizeGripActive]       = rgb(122, 162, 247);

    c[ImGuiCol_Tab]                    = rgb(26, 27, 46);
    c[ImGuiCol_TabHovered]             = rgb(46, 50, 72);
    c[ImGuiCol_TabSelected]            = rgb(36, 40, 59);
    c[ImGuiCol_TabSelectedOverline]    = rgb(122, 162, 247);
    c[ImGuiCol_TabDimmed]              = rgb(20, 21, 38);
    c[ImGuiCol_TabDimmedSelected]      = rgb(30, 33, 52);
    c[ImGuiCol_TabDimmedSelectedOverline] = rgb(90, 128, 210, 0.65F);

    c[ImGuiCol_PlotLines]              = rgb(115, 218, 202); // teal
    c[ImGuiCol_PlotLinesHovered]       = rgb(125, 207, 255); // cyan
    c[ImGuiCol_PlotHistogram]          = rgb(224, 175, 104); // yellow
    c[ImGuiCol_PlotHistogramHovered]   = rgb(255, 158, 100); // orange

    c[ImGuiCol_TableHeaderBg]          = rgb(36, 40, 59);
    c[ImGuiCol_TableBorderStrong]      = rgb(56, 62, 90);
    c[ImGuiCol_TableBorderLight]       = rgb(46, 50, 72);
    c[ImGuiCol_TableRowBg]             = rgb(0, 0, 0, 0.0F);
    c[ImGuiCol_TableRowBgAlt]          = rgb(255, 255, 255, 0.04F);

    c[ImGuiCol_TextSelectedBg]         = rgb(122, 162, 247, 0.28F);
    c[ImGuiCol_TreeLines]              = rgb(86, 95, 137);
    c[ImGuiCol_DragDropTarget]         = rgb(224, 175, 104);
    c[ImGuiCol_DragDropTargetBg]       = rgb(224, 175, 104, 0.14F);
    c[ImGuiCol_UnsavedMarker]          = rgb(255, 158, 100);  // orange
    c[ImGuiCol_NavCursor]              = rgb(187, 154, 247);  // purple
    c[ImGuiCol_NavWindowingHighlight]  = rgb(122, 162, 247, 0.70F);
    c[ImGuiCol_NavWindowingDimBg]      = rgb(0, 0, 0, 0.30F);
    c[ImGuiCol_ModalWindowDimBg]       = rgb(0, 0, 0, 0.45F);
}

} // namespace

// ─── Public API ─────────────────────────────────────────────────────────────

void applyTheme(AppTheme theme) {
    switch (theme) {
        case AppTheme::Light:           applyLightTheme();           break;
        case AppTheme::Dark:            applyDarkTheme();            break;
        case AppTheme::ArecaDark:       applyArecaDarkTheme();       break;
        case AppTheme::Gruvbox:         applyGruvboxTheme();         break;
        case AppTheme::Catppuccin:      applyCatppuccinTheme();      break;
        case AppTheme::CatppuccinLatte: applyCatppuccinLatteTheme(); break;
        case AppTheme::Dracula:         applyDraculaTheme();         break;
        case AppTheme::OneDark:         applyOneDarkTheme();         break;
        case AppTheme::TokyoNight:      applyTokyoNightTheme();      break;
    }
}

ImVec4 themeBackground(AppTheme theme) {
    switch (theme) {
        case AppTheme::Light:           return rgb(247, 245, 237);
        case AppTheme::Dark:            return rgb(18, 18, 22);
        case AppTheme::ArecaDark:       return rgb(18, 26, 23);
        case AppTheme::Gruvbox:         return rgb(40, 40, 40);
        case AppTheme::Catppuccin:      return rgb(30, 30, 46);
        case AppTheme::CatppuccinLatte: return rgb(239, 241, 245);
        case AppTheme::Dracula:         return rgb(40, 42, 54);
        case AppTheme::OneDark:         return rgb(40, 44, 52);
        case AppTheme::TokyoNight:      return rgb(26, 27, 46);
    }
    return rgb(247, 245, 237);
}

} // namespace areca::settings
