#include "ui_views.h"

#include "fcitx_dbus.h"
#include "key_mapper.h"
#include "ui_widgets.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <imgui.h>
#include <string>
#include <vector>

namespace areca::settings {

    constexpr float comboWidth = 280.0F;

    static void pushPrimaryButtonColors() {
        const ImVec4 a = ImGui::GetStyle().Colors[ImGuiCol_SliderGrabActive];
        // Perceived luminance — WCAG formula coefficients
        const float lum = 0.299F * a.x + 0.587F * a.y + 0.114F * a.z;
        const bool brightAccent = (lum > 0.55F);

        // Bright accent (e.g. Gruvbox yellow): dùng nền tối hơn + chữ tối
        // Dark accent (e.g. xanh lá, xanh dương, tím): nền accent + chữ trắng
        const float btnScale = brightAccent ? 0.55F : 1.10F;
        const float hovScale = brightAccent ? 0.45F : 0.90F;
        const float actScale = brightAccent ? 0.35F : 0.72F;
        const ImVec4 textColor = brightAccent ? ImVec4(0.95F, 0.95F, 0.92F, 1.0F) // off-white trên nền tối
                                              : ImVec4(1.0F, 1.0F, 1.0F, 0.95F);  // trắng trên nền accent

        ImGui::PushStyleColor(ImGuiCol_Text, textColor);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(a.x * btnScale, a.y * btnScale, a.z * btnScale, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(a.x * hovScale, a.y * hovScale, a.z * hovScale, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(a.x * actScale, a.y * actScale, a.z * actScale, 1.0F));
    }

    static void pushAttentionButtonColors() {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.95F, 0.66F, 0.23F, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.98F, 0.73F, 0.34F, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.84F, 0.51F, 0.10F, 1.0F));
    }

    static void pushDangerButtonColors() {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.996F, 0.973F, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70F, 0.20F, 0.16F, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.78F, 0.25F, 0.20F, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.58F, 0.14F, 0.12F, 1.0F));
    }

    static void drawArecaIcon(ImDrawList* drawList, const ImVec2 origin, const float size) {
        // Same paths and colors as icons/hicolor/scalable/apps/fcitx-areca.svg.
        const float scale = size / 256.0F;
        const auto point = [origin, scale](const float x, const float y) {
            return ImVec2(origin.x + x * scale, origin.y + y * scale);
        };
        const ImU32 green = IM_COL32(21, 153, 87, 255);
        const ImU32 orange = IM_COL32(242, 169, 59, 255);

        drawList->PathLineTo(point(42.0F, 222.0F));
        drawList->PathLineTo(point(108.0F, 76.0F));
        drawList->PathBezierCubicCurveTo(point(113.0F, 64.0F), point(120.0F, 58.0F), point(128.0F, 58.0F));
        drawList->PathBezierCubicCurveTo(point(136.0F, 58.0F), point(143.0F, 64.0F), point(148.0F, 76.0F));
        drawList->PathLineTo(point(214.0F, 222.0F));
        drawList->PathStroke(green, 32.0F * scale);
        drawList->AddCircleFilled(point(42.0F, 222.0F), 16.0F * scale, green);
        drawList->AddCircleFilled(point(214.0F, 222.0F), 16.0F * scale, green);

        drawList->AddLine(point(78.0F, 164.0F), point(178.0F, 164.0F), green, 28.0F * scale);
        drawList->AddCircleFilled(point(78.0F, 164.0F), 14.0F * scale, green);
        drawList->AddCircleFilled(point(178.0F, 164.0F), 14.0F * scale, green);

        drawList->PathLineTo(point(91.0F, 25.0F));
        drawList->PathBezierCubicCurveTo(point(101.0F, 44.0F), point(113.0F, 53.0F), point(128.0F, 53.0F));
        drawList->PathBezierCubicCurveTo(point(143.0F, 53.0F), point(155.0F, 44.0F), point(165.0F, 25.0F));
        drawList->PathStroke(orange, 17.0F * scale);
        drawList->AddCircleFilled(point(91.0F, 25.0F), 8.5F * scale, orange);
        drawList->AddCircleFilled(point(165.0F, 25.0F), 8.5F * scale, orange);
    }

    static void drawBrandHeader() {
        const ImGuiStyle& style = ImGui::GetStyle();
        ImVec4 headerBg = style.Colors[ImGuiCol_FrameBg];
        headerBg.w = 0.65F;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, headerBg);
        ImGui::PushStyleColor(ImGuiCol_Border, style.Colors[ImGuiCol_Border]);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0F, 11.0F));
        ImGui::BeginChild(
            "BrandHeader", ImVec2(0.0F, 76.0F), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding
        );

        const ImVec2 iconPosition = ImGui::GetCursorScreenPos();
        constexpr float iconSize = 52.0F;
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawArecaIcon(drawList, iconPosition, iconSize);
        ImGui::Dummy(ImVec2(iconSize, iconSize));
        ImGui::SameLine(0.0F, 14.0F);
        ImGui::BeginGroup();
        ImGui::PushFont(nullptr, 24.0F);
        ImGui::TextUnformatted("Areca Settings");
        ImGui::PopFont();
        ImGui::PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_TextDisabled]);
        ImGui::TextUnformatted("Bộ gõ tiếng Việt gọn, nhanh và tự nhiên");
        ImGui::PopStyleColor();
        ImGui::EndGroup();

        ImGui::EndChild();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
        ImGui::Dummy(ImVec2(0.0F, 4.0F));
    }

    static void drawPageIntro(const char* title, const char* description) {
        ImGui::PushFont(nullptr, 22.0F);
        ImGui::TextUnformatted(title);
        ImGui::PopFont();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::TextWrapped("%s", description);
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0.0F, 4.0F));
    }

    static void beginSettingsCard(const char* id, const char* title, const char* description = nullptr) {
        const ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, style.Colors[ImGuiCol_PopupBg]);
        ImGui::PushStyleColor(ImGuiCol_Border, style.Colors[ImGuiCol_Border]);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0F, 14.0F));
        ImGui::BeginChild(
            id, ImVec2(0.0F, 0.0F),
            ImGuiChildFlags_Borders
                | ImGuiChildFlags_AlwaysUseWindowPadding
                | ImGuiChildFlags_AutoResizeY
                | ImGuiChildFlags_AlwaysAutoResize
        );
        ImGui::PushFont(nullptr, 19.0F);
        ImGui::PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_SliderGrab]);
        ImGui::TextUnformatted(title);
        ImGui::PopStyleColor();
        ImGui::PopFont();
        if (description != nullptr) {
            ImGui::PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_TextDisabled]);
            ImGui::TextWrapped("%s", description);
            ImGui::PopStyleColor();
        }
        ImGui::Dummy(ImVec2(0.0F, 3.0F));
    }

    static void endSettingsCard() {
        ImGui::EndChild();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
        ImGui::Dummy(ImVec2(0.0F, 8.0F));
    }

    void drawBasic(
        ConfigStore& config, const std::vector<std::string>& inputMethods, const std::vector<std::string>& charsets,
        bool& listeningShortcut
    ) {
        drawPageIntro("Thiết lập bộ gõ", "Điều chỉnh cách Areca nhận phím và hiển thị tiếng Việt.");

        const float labelWidth = std::max({
                                     ImGui::CalcTextSize("Kiểu gõ").x,
                                     ImGui::CalcTextSize("Bảng mã đầu ra").x,
                                     ImGui::CalcTextSize("Kiểm tra chính tả").x,
                                     ImGui::CalcTextSize("Chế độ hiển thị").x,
                                     ImGui::CalcTextSize("Phím chuyển chế độ:").x,
                                 })
            + ImGui::GetStyle().ItemSpacing.x * 2.0F;

        beginSettingsCard("InputSettingsCard", "Phương thức nhập");
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Kiểu gõ");
        ImGui::SameLine(labelWidth);
        stringCombo("##BambooInputMethod", config.main.bambooInputMethod, inputMethods, comboWidth);

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Bảng mã đầu ra");
        ImGui::SameLine(labelWidth);
        stringCombo("##OutputCharset", config.main.outputCharset, charsets, comboWidth);

        constexpr std::array<const char*, 3> spellcheckNames = {
            "Không kiểm tra (Tắt)", "Khôi phục từ sau khi gõ xong", "Khôi phục từ ngay trong lúc gõ"
        };
        int spellcheck = static_cast<int>(config.main.spellcheckMode.value());
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Kiểm tra chính tả");
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(comboWidth);
        if (ImGui::Combo(
                "##SpellcheckMode", &spellcheck, spellcheckNames.data(), static_cast<int>(spellcheckNames.size())
            )) {
            config.main.spellcheckMode.setValue(static_cast<areca::SpellcheckMode>(spellcheck));
        }

        constexpr std::array<const char*, 3> presentationNames = {"Rewrite trực tiếp", "Preedit", "Redirect (EN)"};
        int presentation = static_cast<int>(config.main.presentationMode.value());
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Chế độ hiển thị");
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(comboWidth);
        if (ImGui::Combo(
                "##PresentationMode", &presentation, presentationNames.data(),
                static_cast<int>(presentationNames.size())
            )) {
            config.main.presentationMode.setValue(static_cast<areca::PresentationMode>(presentation));
        }

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Phím chuyển chế độ:");
        ImGui::SameLine(labelWidth);
        std::string currentShortcut = formatKeyList(config.main.switchModeKey.value());
        if (currentShortcut.empty()) {
            currentShortcut = "Chưa gán";
        }

        if (listeningShortcut) {
            pushAttentionButtonColors();
            if (ImGui::Button("Đang chờ bấm phím... (Esc để hủy)")) {
                listeningShortcut = false;
            }
            ImGui::PopStyleColor(3);
        } else {
            std::string buttonLabel = currentShortcut + "  [Đổi phím]";
            if (ImGui::Button(buttonLabel.c_str())) {
                listeningShortcut = true;
            }
        }
        endSettingsCard();

        beginSettingsCard(
            "TypingBehaviorCard", "Hành vi khi gõ", "Bật các tiện ích tự động và lựa chọn tương thích ứng dụng."
        );
        const int behaviorColumns = ImGui::GetContentRegionAvail().x >= 760.0F ? 2 : 1;
        if (ImGui::BeginTable("TypingBehaviorGrid", behaviorColumns, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextColumn();
            checkbox("Đặt dấu kiểu mới (oà, uý)", config.main.modernStyle);
            ImGui::TableNextColumn();
            checkbox("Tự viết hoa sau . ! ?", config.main.autoCapitalizeAfterPunctuation);
            ImGui::TableNextColumn();
            checkbox("Khôi phục chính tả khi nhấn Backspace", config.main.backspaceRecovery);
            ImGui::TableNextColumn();
            checkbox("Fallback Shift+Left cho trình duyệt", config.main.shiftSelectFallbackForBrowser);
            ImGui::TableNextColumn();
            checkbox("Bật macro", config.main.enableMacro);
            ImGui::TableNextColumn();
            checkbox("Đổi hoa/thường nội dung macro", config.main.capitalizeMacro);
            ImGui::EndTable();
        }
        endSettingsCard();

        beginSettingsCard("DiagnosticsCard", "Chẩn đoán");
        checkbox("Bật log debug Areca", config.main.debug);
        ImGui::SameLine();
        ImGui::TextWrapped("Log xuất hiện trong journal của tiến trình fcitx5.");
        endSettingsCard();
    }

    bool drawMacros(ConfigStore& config, size_t& pendingDeleteIndex) {
        drawPageIntro("Macro", "Mở rộng từ viết tắt thành nội dung thường dùng khi đang gõ.");
        beginSettingsCard("MacroListCard", "Danh sách macro", "Các mục bên dưới được lưu cùng cấu hình Fcitx5.");
        auto& entries = *config.macros.macros.mutableValue();

        pushPrimaryButtonColors();
        static bool focusNewMacro = false;
        static char searchQuery[256] = "";
        if (ImGui::Button("+  Thêm macro")) {
            areca::MacroEntry entry;
            entries.insert(entries.begin(), std::move(entry));
            focusNewMacro = true;
            searchQuery[0] = '\0';
        }
        ImGui::PopStyleColor(4);
        
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0F);
        ImGui::InputTextWithHint("##search", "Tìm kiếm (Ctrl+F)...", searchQuery, sizeof(searchQuery));
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
            ImGui::SetKeyboardFocusHere(-1);
        }
        std::string query(searchQuery);
        auto to_lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c; };
        std::transform(query.begin(), query.end(), query.begin(), to_lower);

        std::vector<size_t> visibleIndices;
        visibleIndices.reserve(entries.size());
        int invalidCount = 0;

        for (size_t index = 0; index < entries.size(); ++index) {
            if (!query.empty()) {
                std::string key = entries[index].key.value();
                std::string val = entries[index].value.value();
                std::transform(key.begin(), key.end(), key.begin(), to_lower);
                std::transform(val.begin(), val.end(), val.begin(), to_lower);
                if (key.find(query) == std::string::npos && val.find(query) == std::string::npos) {
                    continue;
                }
            }
            visibleIndices.push_back(index);
            if (entries[index].key.value().empty() || entries[index].value.value().empty()) {
                ++invalidCount;
            }
        }

        ImGui::SameLine();
        if (invalidCount > 0) {
            ImGui::TextColored(
                ImVec4(0.70F, 0.20F, 0.16F, 1.0F), "%zu macro, %d dòng lỗi", visibleIndices.size(), invalidCount
            );
        } else {
            ImGui::TextDisabled("%zu macro", visibleIndices.size());
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            pendingDeleteIndex = SIZE_MAX;
        }
        size_t removeIndex = entries.size();
        bool shouldOpenDeletePopup = false;
        if (ImGui::BeginTable(
                "macro-table", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable
            )) {
            ImGui::TableSetupColumn("Từ viết tắt", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Nội dung thay thế", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 120.0F);
            ImGui::TableHeadersRow();
            for (size_t index : visibleIndices) {

                ImGui::PushID(static_cast<int>(index));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                bool emptyKey = entries[index].key.value().empty();
                if (emptyKey) {
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.99F, 0.89F, 0.87F, 1.0F));
                }

                if (index == 0 && focusNewMacro) {
                    ImGui::SetKeyboardFocusHere(0);
                    focusNewMacro = false;
                }

                inputText("##key", entries[index].key);
                if (emptyKey) {
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Tên macro không được rỗng");
                    }
                }
                ImGui::TableSetColumnIndex(1);
                bool emptyVal = entries[index].value.value().empty();
                if (emptyVal) {
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.99F, 0.89F, 0.87F, 1.0F));
                }
                inputText("##value", entries[index].value);
                if (emptyVal) {
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Nội dung thay thế không được rỗng");
                    }
                }
                ImGui::TableSetColumnIndex(2);

                const float btnWidth = 64.0F;
                const float columnWidth = ImGui::GetContentRegionAvail().x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, (columnWidth - btnWidth) * 0.5F));

                if (ImGui::Button("Xóa", ImVec2(btnWidth, 0.0F))) {
                    pendingDeleteIndex = index;
                    shouldOpenDeletePopup = true;
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        if (shouldOpenDeletePopup) {
            ImGui::OpenPopup("Xác nhận xóa macro");
        }

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
        if (ImGui::BeginPopupModal("Xác nhận xóa macro", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (pendingDeleteIndex < entries.size()) {
                const std::string& key = entries[pendingDeleteIndex].key.value();
                if (key.empty()) {
                    ImGui::TextUnformatted("Bạn có chắc chắn muốn xóa macro này không?");
                } else {
                    ImGui::Text("Bạn có chắc chắn muốn xóa macro \"%s\" không?", key.c_str());
                }
                ImGui::Dummy(ImVec2(0.0F, 12.0F));

                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0F, 6.0F));

                const float btnWidth = 76.0F;
                const float totalWidth = btnWidth * 2.0F + ImGui::GetStyle().ItemSpacing.x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - totalWidth);

                pushDangerButtonColors();
                if (ImGui::Button("Xóa", ImVec2(btnWidth, 0.0F))) {
                    removeIndex = pendingDeleteIndex;
                    ImGui::CloseCurrentPopup();
                    pendingDeleteIndex = SIZE_MAX;
                }
                ImGui::PopStyleColor(4);

                ImGui::SameLine();

                if (ImGui::Button("Hủy", ImVec2(btnWidth, 0.0F))) {
                    ImGui::CloseCurrentPopup();
                    pendingDeleteIndex = SIZE_MAX;
                }
                ImGui::PopStyleVar();
            } else {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        bool macroDeleted = false;
        if (removeIndex != entries.size()) {
            entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(removeIndex));
            if (pendingDeleteIndex == entries.size()) {
                pendingDeleteIndex = SIZE_MAX;
            }
            macroDeleted = true;
        }
        endSettingsCard();
        return macroDeleted;
    }

    void drawAdvanced(ConfigStore& config) {
        drawPageIntro(
            "Thiết lập nâng cao", "Chỉ thay đổi các giá trị này khi một ứng dụng hoặc frontend cụ thể gặp lỗi timing."
        );

        beginSettingsCard("BackspaceTimingCard", "uinput Backspace", "Độ trễ khi mô phỏng thao tác xoá ký tự.");
        inputInt("Delay giữa Backspace (ms)", config.advanced.backspaceDelayMs);
        inputInt("Chờ sau Backspace (ms)", config.advanced.afterBackspaceWaitMs);
        inputInt("Chờ sau Backspace Wayland (ms)", config.advanced.waylandAfterBackspaceWaitMs);
        inputInt("Chờ sau Backspace XIM (ms)", config.advanced.ximAfterBackspaceWaitMs);
        inputInt("Chờ sau Backspace Fcitx4 (ms)", config.advanced.fcitx4AfterBackspaceWaitMs);
        inputInt("Chờ sau Backspace DBus (ms)", config.advanced.dbusAfterBackspaceWaitMs);
        endSettingsCard();

        beginSettingsCard(
            "ShiftSelectTimingCard", "uinput Shift+Left", "Độ trễ khi chọn lại phần văn bản cần thay thế."
        );
        inputInt("Delay giữa uinput Shift+Left (ms)", config.advanced.uinputShiftSelectDelayMs);
        inputInt("Chờ sau uinput Shift+Left (ms)", config.advanced.afterUinputShiftSelectWaitMs);
        inputInt("Chờ sau uinput Shift+Left Wayland (ms)", config.advanced.waylandAfterUinputShiftSelectWaitMs);
        inputInt("Chờ sau uinput Shift+Left XIM (ms)", config.advanced.ximAfterUinputShiftSelectWaitMs);
        inputInt("Chờ sau uinput Shift+Left Fcitx4 (ms)", config.advanced.fcitx4AfterUinputShiftSelectWaitMs);
        inputInt("Chờ sau uinput Shift+Left DBus (ms)", config.advanced.dbusAfterUinputShiftSelectWaitMs);
        endSettingsCard();

        beginSettingsCard(
            "SurroundingTextCard", "Surrounding text", "Timing cho backend xử lý văn bản xung quanh con trỏ."
        );
        inputInt("Chờ sau xóa surrounding text (ms)", config.advanced.surroundingWaitMs);
        inputInt("Delay giữa các lệnh xóa surrounding v2 (ms)", config.advanced.surroundingDeleteDelayMs);
        inputInt(
            "Delay giữa các lệnh xóa surrounding v2 Wayland (ms)", config.advanced.waylandSurroundingDeleteDelayMs
        );
        inputInt("Chờ sau lệnh xóa surrounding v2 cuối (ms)", config.advanced.afterSurroundingDeleteWaitMs);
        endSettingsCard();

        beginSettingsCard("GeneralTimingCard", "Timing chung");
        inputInt("Delay sau commit (ms)", config.advanced.postCommitDelayMs);
        endSettingsCard();

        beginSettingsCard(
            "CompatibilityCard", "Tương thích", "Các cơ chế fallback dành cho ứng dụng có hành vi nhập liệu đặc biệt."
        );
        checkbox("Dùng timer độ chính xác cao", config.advanced.preciseTiming);
        checkbox("Ép dùng uinput thay cho forward Backspace", config.advanced.forceUinput);
        checkbox("Ép uinput Shift+Left cho trình duyệt", config.advanced.useUinputShiftSelectForBrowser);
        checkbox("Ép surrounding text v2 cho trình duyệt", config.advanced.useSurroundingV2ForBrowser);
        checkbox("Tự reset bộ gõ sau khi click chuột trong trình duyệt", config.advanced.enableMouseTracking);
        checkbox("Chuyển tiếp phím đầu để tương thích trình duyệt", config.advanced.forwardFirstCharacter);
        endSettingsCard();
    }

    void drawAppearance(AppConfig& appConfig, bool& needReapplyTheme) {
        drawPageIntro("Giao diện", "Tuỳ chỉnh chủ đề màu sắc và cỡ chữ của bảng thiết lập Areca.");

        const float labelWidth = std::max({
                                     ImGui::CalcTextSize("Chủ đề (Theme)").x,
                                     ImGui::CalcTextSize("Cỡ chữ (Font size)").x,
                                 })
            + ImGui::GetStyle().ItemSpacing.x * 2.0F;

        beginSettingsCard("ThemeCard", "Chủ đề màu sắc");
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Chủ đề (Theme)");
        ImGui::SameLine(labelWidth);

        constexpr std::array<const char*, 9> themeDisplayNames = {
            "Light",   "Dark",     "Areca Dark", "Gruvbox", "Catppuccin Mocha", "Catppuccin Latte",
            "Dracula", "One Dark", "Tokyo Night"
        };
        int currentThemeIdx = static_cast<int>(appConfig.theme);
        ImGui::SetNextItemWidth(comboWidth);
        if (ImGui::Combo(
                "##AppThemeCombo", &currentThemeIdx, themeDisplayNames.data(),
                static_cast<int>(themeDisplayNames.size())
            )) {
            appConfig.theme = static_cast<AppTheme>(currentThemeIdx);
            needReapplyTheme = true;
        }
        endSettingsCard();

        beginSettingsCard(
            "FontCard", "Kích thước phông chữ", "Thay đổi cỡ chữ sẽ có hiệu lực sau khi khởi động lại ứng dụng."
        );
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Cỡ chữ (Font size)");
        ImGui::SameLine(labelWidth);

        int currentFontSize = static_cast<int>(appConfig.fontSize);
        ImGui::SetNextItemWidth(comboWidth);
        ImGui::SliderInt(
            "##AppFontSizeSlider", &currentFontSize, static_cast<int>(kMinFontSize), static_cast<int>(kMaxFontSize),
            "%d px"
        );
        appConfig.fontSize = static_cast<float>(currentFontSize);
        endSettingsCard();
    }

    void drawWindow(
        ConfigStore& config, AppConfig& appConfig, const std::vector<std::string>& inputMethods,
        const std::vector<std::string>& charsets, bool& listeningShortcut, std::string& status, bool& running,
        bool& needReapplyTheme
    ) {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin(
            "Areca Settings", nullptr,
            ImGuiWindowFlags_NoResize
                | ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_NoCollapse
                | ImGuiWindowFlags_NoTitleBar
        );

        const float footerHeight = ImGui::GetFrameHeightWithSpacing() * (status.empty() ? 2.7F : 3.5F);
        static bool confirmReset = false;
        static int activeTab = 0;
        static int prevTab = 0;
        static size_t pendingDeleteIndex = SIZE_MAX;
        static ConfigStore savedConfig;
        static AppConfig savedAppConfig;
        static bool savedConfigInitialized = false;
        if (!savedConfigInitialized) {
            savedConfig = config;
            savedAppConfig = appConfig;
            savedConfigInitialized = true;
        }
        int newActiveTab = activeTab;

        drawBrandHeader();

        bool triggerAutoSave = false;
        if (ImGui::BeginTabBar("SettingsTabBar", ImGuiTabBarFlags_None)) {
            if (ImGui::BeginTabItem("Bộ gõ")) {
                newActiveTab = 0;
                ImGui::BeginChild(
                    "BasicScroll", ImVec2(0.0F, -footerHeight), ImGuiChildFlags_AlwaysUseWindowPadding,
                    ImGuiWindowFlags_HorizontalScrollbar
                );
                drawBasic(config, inputMethods, charsets, listeningShortcut);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Macro")) {
                newActiveTab = 1;
                ImGui::BeginChild(
                    "MacroScroll", ImVec2(0.0F, -footerHeight), ImGuiChildFlags_AlwaysUseWindowPadding,
                    ImGuiWindowFlags_HorizontalScrollbar
                );
                if (drawMacros(config, pendingDeleteIndex)) {
                    triggerAutoSave = true;
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Nâng cao")) {
                newActiveTab = 2;
                ImGui::BeginChild(
                    "AdvancedScroll", ImVec2(0.0F, -footerHeight), ImGuiChildFlags_AlwaysUseWindowPadding,
                    ImGuiWindowFlags_HorizontalScrollbar
                );
                drawAdvanced(config);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Giao diện")) {
                newActiveTab = 3;
                ImGui::BeginChild(
                    "AppearanceScroll", ImVec2(0.0F, -footerHeight), ImGuiChildFlags_AlwaysUseWindowPadding,
                    ImGuiWindowFlags_HorizontalScrollbar
                );
                drawAppearance(appConfig, needReapplyTheme);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        bool hasInvalidMacros = false;
        for (const auto& e : *config.macros.macros.mutableValue()) {
            if (e.key.value().empty() || e.value.value().empty()) {
                hasInvalidMacros = true;
                break;
            }
        }

        if (triggerAutoSave) {
            if (hasInvalidMacros) {
                status = "Đã xóa macro, nhưng chưa lưu vì có macro bị rỗng thông tin.";
            } else {
                config.save();
                savedConfig = config;
                std::string reloadError;
                status = reloadArecaAddon(reloadError) ? "Đã xóa macro thành công."
                                                       : "Đã xóa macro, nhưng Areca chưa áp dụng: " + reloadError;
            }
        }

        if (newActiveTab != prevTab) {
            confirmReset = false;
            pendingDeleteIndex = SIZE_MAX;
            prevTab = newActiveTab;
        }
        activeTab = newActiveTab;

        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            confirmReset = false;
            pendingDeleteIndex = SIZE_MAX;
        }

        areca::ArecaConfig mainWithoutFallback = config.main;
        mainWithoutFallback.shiftSelectFallbackForBrowser.setValue(
            savedConfig.main.shiftSelectFallbackForBrowser.value()
        );
        const bool tab0Dirty = !(mainWithoutFallback == savedConfig.main);

        const bool tab1Dirty = !(config.macros == savedConfig.macros);

        const bool tab2Dirty = [&] {
            const bool fallbackDirty =
                (config.main.shiftSelectFallbackForBrowser.value()
                 != savedConfig.main.shiftSelectFallbackForBrowser.value());
            return !(config.advanced == savedConfig.advanced) || fallbackDirty;
        }();

        const bool tab3Dirty =
            (appConfig.theme != savedAppConfig.theme) || (appConfig.fontSize != savedAppConfig.fontSize);

        const bool currentTabDirty = (activeTab == 0) ? tab0Dirty
            : (activeTab == 1)                        ? tab1Dirty
            : (activeTab == 2)                        ? tab2Dirty
                                                      : tab3Dirty;

        const areca::ArecaConfig defaultMain{};
        areca::ArecaConfig mainForDefaultCheck = config.main;
        mainForDefaultCheck.shiftSelectFallbackForBrowser.setValue(defaultMain.shiftSelectFallbackForBrowser.value());
        const bool tab0IsDefault = (mainForDefaultCheck == defaultMain);

        const bool tab1IsDefault = (config.macros == areca::MacroTableConfig{});

        const bool tab2IsDefault = [&] {
            const bool fallbackIsDefault =
                (config.main.shiftSelectFallbackForBrowser.value()
                 == defaultMain.shiftSelectFallbackForBrowser.value());
            return (config.advanced == areca::AdvancedConfig{}) && fallbackIsDefault;
        }();

        const bool tab3IsDefault = (appConfig.theme == AppTheme::Light) && (appConfig.fontSize == kDefaultFontSize);

        const bool currentTabIsDefault = (activeTab == 0) ? tab0IsDefault
            : (activeTab == 1)                            ? tab1IsDefault
            : (activeTab == 2)                            ? tab2IsDefault
                                                          : tab3IsDefault;

        const ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, style.Colors[ImGuiCol_PopupBg]);
        ImGui::PushStyleColor(ImGuiCol_Border, style.Colors[ImGuiCol_Border]);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 11.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0F, 10.0F));
        ImGui::BeginChild(
            "ActionBar", ImVec2(0.0F, 0.0F), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding
        );
        
        ImGui::BeginDisabled(!currentTabDirty || hasInvalidMacros);
        pushPrimaryButtonColors();
        if (ImGui::Button("Lưu và áp dụng", ImVec2(150.0F, 0.0F))) {
            if (activeTab == 3) {
                const bool fontChanged = (appConfig.fontSize != savedAppConfig.fontSize);
                appConfig.save();
                savedAppConfig = appConfig;
                status =
                    fontChanged ? "Đã lưu. Khởi động lại ứng dụng để áp dụng cỡ chữ mới." : "Đã lưu cài đặt giao diện.";
            } else {
                config.save();
                savedConfig = config;
                std::string reloadError;
                status = reloadArecaAddon(reloadError) ? "Đã lưu và Areca đang chạy đã tải lại cấu hình."
                                                       : "Đã lưu file, nhưng Areca chưa áp dụng: " + reloadError;
            }
            confirmReset = false;
        }
        ImGui::PopStyleColor(4);
        ImGui::EndDisabled();

        if (hasInvalidMacros && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Vui lòng xóa hoặc điền đầy đủ từ viết tắt và nội dung thay thế cho các macro trước khi lưu.");
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(!currentTabDirty);
        if (ImGui::Button("Huỷ các thay đổi")) {
            if (activeTab == 0) {
                const bool currentFallback = config.main.shiftSelectFallbackForBrowser.value();
                config.main = savedConfig.main;
                config.main.shiftSelectFallbackForBrowser.setValue(currentFallback);
                listeningShortcut = false;
            } else if (activeTab == 1) {
                config.macros = savedConfig.macros;
                pendingDeleteIndex = SIZE_MAX;
            } else if (activeTab == 2) {
                config.advanced = savedConfig.advanced;
                config.main.shiftSelectFallbackForBrowser.setValue(
                    savedConfig.main.shiftSelectFallbackForBrowser.value()
                );
            } else {
                if (appConfig.theme != savedAppConfig.theme) {
                    needReapplyTheme = true;
                }
                appConfig = savedAppConfig;
            }
            confirmReset = false;
            status = "Đã huỷ các thay đổi";
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (!confirmReset) {
            ImGui::BeginDisabled(currentTabIsDefault);
            if (ImGui::Button("Khôi phục mặc định")) {
                confirmReset = true;
            }
            ImGui::EndDisabled();
        } else {
            pushDangerButtonColors();
            if (ImGui::Button("Xác nhận khôi phục?")) {
                if (activeTab == 0) {
                    const bool currentFallback = config.main.shiftSelectFallbackForBrowser.value();
                    config.main = areca::ArecaConfig{};
                    config.main.shiftSelectFallbackForBrowser.setValue(currentFallback);
                    listeningShortcut = false;
                    config.save();
                    savedConfig = config;
                    std::string reloadError;
                    status = reloadArecaAddon(reloadError)
                        ? "Đã khôi phục và áp dụng."
                        : "Đã khôi phục và lưu, nhưng Areca chưa áp dụng: " + reloadError;
                } else if (activeTab == 1) {
                    config.macros = areca::MacroTableConfig{};
                    config.save();
                    savedConfig = config;
                    std::string reloadError;
                    status = reloadArecaAddon(reloadError)
                        ? "Đã khôi phục và áp dụng."
                        : "Đã khôi phục và lưu, nhưng Areca chưa áp dụng: " + reloadError;
                } else if (activeTab == 2) {
                    config.advanced = areca::AdvancedConfig{};
                    config.main.shiftSelectFallbackForBrowser.setValue(
                        areca::ArecaConfig{}.shiftSelectFallbackForBrowser.value()
                    );
                    config.save();
                    savedConfig = config;
                    std::string reloadError;
                    status = reloadArecaAddon(reloadError)
                        ? "Đã khôi phục và áp dụng."
                        : "Đã khôi phục và lưu, nhưng Areca chưa áp dụng: " + reloadError;
                } else {
                    if (appConfig.theme != AppTheme::Light) {
                        needReapplyTheme = true;
                    }
                    appConfig.theme = AppTheme::Light;
                    appConfig.fontSize = kDefaultFontSize;
                    appConfig.save();
                    savedAppConfig = appConfig;
                    status = "Đã khôi phục giao diện mặc định. Khởi động lại ứng dụng nếu cỡ chữ đã thay đổi.";
                }
                confirmReset = false;
            }
            ImGui::PopStyleColor(4);
            ImGui::SameLine();
            if (ImGui::Button("Hủy")) {
                confirmReset = false;
            }
        }
        const float exitButtonWidth = 90.0F;
        ImGui::SameLine(ImGui::GetWindowWidth() - exitButtonWidth - ImGui::GetStyle().WindowPadding.x);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.78F, 0.25F, 0.20F, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.58F, 0.14F, 0.12F, 1.0F));
        if (ImGui::Button("Thoát", ImVec2(exitButtonWidth, 0.0F))) {
            running = false;
        }
        ImGui::PopStyleColor(2);
        if (!status.empty()) {
            const bool warning =
                status.find("nhưng") != std::string::npos || status.find("Khởi động lại") != std::string::npos;
            const ImVec4* tc = ImGui::GetStyle().Colors;
            ImGui::TextColored(warning ? tc[ImGuiCol_UnsavedMarker] : tc[ImGuiCol_SliderGrab], "%s", status.c_str());
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
        ImGui::End();
    }

} // namespace areca::settings
