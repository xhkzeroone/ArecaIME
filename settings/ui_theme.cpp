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

} // namespace

void applyArecaTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsLight(&style);

    style.WindowPadding = ImVec2(18.0F, 16.0F);
    style.FramePadding = ImVec2(10.0F, 7.0F);
    style.ItemSpacing = ImVec2(10.0F, 9.0F);
    style.ItemInnerSpacing = ImVec2(8.0F, 6.0F);
    style.CellPadding = ImVec2(10.0F, 8.0F);
    style.ScrollbarSize = 13.0F;
    style.GrabMinSize = 12.0F;

    style.WindowRounding = 0.0F;
    style.ChildRounding = 10.0F;
    style.PopupRounding = 10.0F;
    style.FrameRounding = 8.0F;
    style.ScrollbarRounding = 8.0F;
    style.GrabRounding = 6.0F;
    style.TabRounding = 8.0F;
    style.MenuItemRounding = 6.0F;

    style.WindowBorderSize = 0.0F;
    style.ChildBorderSize = 0.0F;
    style.PopupBorderSize = 1.0F;
    style.FrameBorderSize = 1.0F;
    style.TabBorderSize = 0.0F;
    style.TabBarBorderSize = 1.0F;
    style.TabBarOverlineSize = 3.0F;
    style.TabMinWidthBase = 118.0F;
    style.SeparatorTextBorderSize = 1.0F;
    style.SeparatorTextPadding = ImVec2(12.0F, 7.0F);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = rgb(16, 33, 28);                    // --ink
    colors[ImGuiCol_TextDisabled] = rgb(95, 112, 106);          // --muted
    colors[ImGuiCol_WindowBg] = rgb(247, 245, 237);             // --paper
    colors[ImGuiCol_ChildBg] = rgb(247, 245, 237);
    colors[ImGuiCol_PopupBg] = rgb(255, 254, 248);              // warm white
    colors[ImGuiCol_Border] = rgb(16, 33, 28, 0.18F);          // --line
    colors[ImGuiCol_BorderShadow] = rgb(7, 24, 19, 0.0F);

    colors[ImGuiCol_FrameBg] = rgb(255, 254, 248);
    colors[ImGuiCol_FrameBgHovered] = rgb(232, 242, 233);
    colors[ImGuiCol_FrameBgActive] = rgb(218, 235, 223);
    colors[ImGuiCol_TitleBg] = rgb(236, 234, 221);              // --paper-deep
    colors[ImGuiCol_TitleBgActive] = rgb(218, 235, 223);
    colors[ImGuiCol_TitleBgCollapsed] = rgb(236, 234, 221);
    colors[ImGuiCol_MenuBarBg] = rgb(247, 245, 237);

    colors[ImGuiCol_ScrollbarBg] = rgb(236, 234, 221, 0.72F);
    colors[ImGuiCol_ScrollbarGrab] = rgb(159, 177, 168);
    colors[ImGuiCol_ScrollbarGrabHovered] = rgb(95, 112, 106);
    colors[ImGuiCol_ScrollbarGrabActive] = rgb(11, 95, 58);     // --green-dark
    colors[ImGuiCol_CheckMark] = rgb(11, 95, 58);
    colors[ImGuiCol_CheckboxSelectedBg] = rgb(214, 235, 221);
    colors[ImGuiCol_SliderGrab] = rgb(21, 153, 87);             // --green
    colors[ImGuiCol_SliderGrabActive] = rgb(11, 95, 58);

    colors[ImGuiCol_Button] = rgb(226, 237, 228);
    colors[ImGuiCol_ButtonHovered] = rgb(204, 228, 212);
    colors[ImGuiCol_ButtonActive] = rgb(178, 214, 190);
    colors[ImGuiCol_Header] = rgb(221, 238, 226);
    colors[ImGuiCol_HeaderHovered] = rgb(204, 228, 212);
    colors[ImGuiCol_HeaderActive] = rgb(178, 214, 190);

    colors[ImGuiCol_Separator] = rgb(16, 33, 28, 0.16F);
    colors[ImGuiCol_SeparatorHovered] = rgb(21, 153, 87, 0.70F);
    colors[ImGuiCol_SeparatorActive] = rgb(11, 95, 58);
    colors[ImGuiCol_ResizeGrip] = rgb(21, 153, 87, 0.20F);
    colors[ImGuiCol_ResizeGripHovered] = rgb(21, 153, 87, 0.65F);
    colors[ImGuiCol_ResizeGripActive] = rgb(11, 95, 58);
    colors[ImGuiCol_InputTextCursor] = rgb(11, 95, 58);

    colors[ImGuiCol_Tab] = rgb(236, 234, 221);
    colors[ImGuiCol_TabHovered] = rgb(204, 228, 212);
    colors[ImGuiCol_TabSelected] = rgb(221, 238, 226);
    colors[ImGuiCol_TabSelectedOverline] = rgb(21, 153, 87);
    colors[ImGuiCol_TabDimmed] = rgb(240, 238, 228);
    colors[ImGuiCol_TabDimmedSelected] = rgb(226, 237, 228);
    colors[ImGuiCol_TabDimmedSelectedOverline] = rgb(11, 95, 58, 0.65F);

    colors[ImGuiCol_PlotLines] = rgb(21, 153, 87);
    colors[ImGuiCol_PlotLinesHovered] = rgb(11, 95, 58);
    colors[ImGuiCol_PlotHistogram] = rgb(242, 169, 59);
    colors[ImGuiCol_PlotHistogramHovered] = rgb(180, 83, 9);
    colors[ImGuiCol_TableHeaderBg] = rgb(236, 234, 221);
    colors[ImGuiCol_TableBorderStrong] = rgb(197, 200, 191);
    colors[ImGuiCol_TableBorderLight] = rgb(218, 219, 210);
    colors[ImGuiCol_TableRowBg] = rgb(255, 254, 248, 0.45F);
    colors[ImGuiCol_TableRowBgAlt] = rgb(236, 234, 221, 0.55F);

    colors[ImGuiCol_TextLink] = rgb(11, 95, 58);
    colors[ImGuiCol_TextSelectedBg] = rgb(21, 153, 87, 0.30F);
    colors[ImGuiCol_TreeLines] = rgb(95, 112, 106);
    colors[ImGuiCol_DragDropTarget] = rgb(242, 169, 59);        // --orange
    colors[ImGuiCol_DragDropTargetBg] = rgb(242, 169, 59, 0.16F);
    colors[ImGuiCol_UnsavedMarker] = rgb(242, 169, 59);
    colors[ImGuiCol_NavCursor] = rgb(242, 169, 59);
    colors[ImGuiCol_NavWindowingHighlight] = rgb(21, 153, 87, 0.75F);
    colors[ImGuiCol_NavWindowingDimBg] = rgb(7, 24, 19, 0.18F);
    colors[ImGuiCol_ModalWindowDimBg] = rgb(7, 24, 19, 0.28F);
}

ImVec4 arecaThemeBackground() {
    return rgb(247, 245, 237);
}

} // namespace areca::settings
