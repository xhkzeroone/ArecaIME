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

    void drawBasic(
        ConfigStore& config, const std::vector<std::string>& inputMethods, const std::vector<std::string>& charsets,
        bool& listeningShortcut
    ) {
        const float labelWidth = std::max({
                                     ImGui::CalcTextSize("Kiểu gõ").x,
                                     ImGui::CalcTextSize("Bảng mã đầu ra").x,
                                     ImGui::CalcTextSize("Kiểm tra chính tả").x,
                                     ImGui::CalcTextSize("Chế độ hiển thị").x,
                                     ImGui::CalcTextSize("Phím chuyển chế độ:").x,
                                 })
            + ImGui::GetStyle().ItemSpacing.x * 2.0F;

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
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7F, 0.2F, 0.2F, 1.0F));
            if (ImGui::Button("Đang chờ bấm phím... (Esc để hủy)")) {
                listeningShortcut = false;
            }
            ImGui::PopStyleColor();
        } else {
            std::string buttonLabel = currentShortcut + "  [Đổi phím]";
            if (ImGui::Button(buttonLabel.c_str())) {
                listeningShortcut = true;
            }
        }

        ImGui::Separator();
        checkbox("Đặt dấu kiểu mới (oà, uý)", config.main.modernStyle);
        checkbox("Tự viết hoa sau . ! ?", config.main.autoCapitalizeAfterPunctuation);
        checkbox("Khôi phục chính tả khi nhấn Backspace", config.main.backspaceRecovery);
        checkbox("Fallback Shift+Left cho trình duyệt", config.main.shiftSelectFallbackForBrowser);
        checkbox("Bật macro", config.main.enableMacro);
        checkbox("Đổi hoa/thường nội dung macro", config.main.capitalizeMacro);

        ImGui::SeparatorText("Debug");
        checkbox("Bật log debug Areca", config.main.debug);
        ImGui::TextWrapped("Log xuất hiện trong journal của tiến trình fcitx5.");
    }

    void drawMacros(ConfigStore& config, size_t& pendingDeleteIndex) {
        ImGui::TextWrapped("Macro được lưu cùng cấu hình Fcitx5.");
        auto& entries = *config.macros.macros.mutableValue();
        int invalidCount = 0;
        for (const auto& e : entries) {
            if (e.key.value().empty()) {
                ++invalidCount;
            }
        }
        if (ImGui::Button("Thêm macro")) {
            areca::MacroEntry entry;
            entries.push_back(std::move(entry));
        }
        ImGui::SameLine();
        if (invalidCount > 0) {
            ImGui::TextColored(ImVec4(0.9F, 0.3F, 0.3F, 1.0F), "%zu macro, %d tên rỗng", entries.size(), invalidCount);
        } else {
            ImGui::TextDisabled("%zu macro", entries.size());
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            pendingDeleteIndex = SIZE_MAX;
        }
        size_t removeIndex = entries.size();
        if (ImGui::BeginTable(
                "macro-table", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable
            )) {
            ImGui::TableSetupColumn("Từ viết tắt", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Nội dung thay thế", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 120.0F);
            ImGui::TableHeadersRow();
            for (size_t index = 0; index < entries.size(); ++index) {
                ImGui::PushID(static_cast<int>(index));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                bool emptyKey = entries[index].key.value().empty();
                if (emptyKey) {
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.4F, 0.1F, 0.1F, 1.0F));
                }
                inputText("##key", entries[index].key);
                if (emptyKey) {
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Tên macro không được rỗng");
                    }
                }
                ImGui::TableSetColumnIndex(1);
                inputText("##value", entries[index].value);
                ImGui::TableSetColumnIndex(2);
                if (pendingDeleteIndex == index) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7F, 0.2F, 0.2F, 1.0F));
                    if (ImGui::SmallButton("Xác nhận")) {
                        removeIndex = index;
                        pendingDeleteIndex = SIZE_MAX;
                    }
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Hủy")) {
                        pendingDeleteIndex = SIZE_MAX;
                    }
                } else {
                    if (ImGui::SmallButton("Xóa")) {
                        pendingDeleteIndex = index;
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (removeIndex != entries.size()) {
            entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(removeIndex));
            if (pendingDeleteIndex == entries.size()) {
                pendingDeleteIndex = SIZE_MAX;
            }
        }
    }

    void drawAdvanced(ConfigStore& config) {
        ImGui::TextWrapped(
            "Các giá trị này chỉ cần thay đổi khi một ứng dụng hoặc "
            "frontend cụ thể gặp lỗi timing."
        );
        ImGui::SeparatorText("uinput Backspace");
        inputInt("Delay giữa Backspace (ms)", config.advanced.backspaceDelayMs);
        inputInt("Chờ sau Backspace (ms)", config.advanced.afterBackspaceWaitMs);
        inputInt("Chờ sau Backspace Wayland (ms)", config.advanced.waylandAfterBackspaceWaitMs);
        inputInt("Chờ sau Backspace XIM (ms)", config.advanced.ximAfterBackspaceWaitMs);
        inputInt("Chờ sau Backspace Fcitx4 (ms)", config.advanced.fcitx4AfterBackspaceWaitMs);
        inputInt("Chờ sau Backspace DBus (ms)", config.advanced.dbusAfterBackspaceWaitMs);

        ImGui::SeparatorText("uinput Shift+Left");
        inputInt("Delay giữa uinput Shift+Left (ms)", config.advanced.uinputShiftSelectDelayMs);
        inputInt("Chờ sau uinput Shift+Left (ms)", config.advanced.afterUinputShiftSelectWaitMs);
        inputInt("Chờ sau uinput Shift+Left Wayland (ms)", config.advanced.waylandAfterUinputShiftSelectWaitMs);
        inputInt("Chờ sau uinput Shift+Left XIM (ms)", config.advanced.ximAfterUinputShiftSelectWaitMs);
        inputInt("Chờ sau uinput Shift+Left Fcitx4 (ms)", config.advanced.fcitx4AfterUinputShiftSelectWaitMs);
        inputInt("Chờ sau uinput Shift+Left DBus (ms)", config.advanced.dbusAfterUinputShiftSelectWaitMs);

        ImGui::SeparatorText("Surrounding text");
        inputInt("Chờ sau xóa surrounding text (ms)", config.advanced.surroundingWaitMs);
        inputInt("Delay giữa các lệnh xóa surrounding v2 (ms)", config.advanced.surroundingDeleteDelayMs);
        inputInt(
            "Delay giữa các lệnh xóa surrounding v2 Wayland (ms)", config.advanced.waylandSurroundingDeleteDelayMs
        );
        inputInt("Chờ sau lệnh xóa surrounding v2 cuối (ms)", config.advanced.afterSurroundingDeleteWaitMs);

        ImGui::SeparatorText("Timing chung");
        inputInt("Delay sau commit (ms)", config.advanced.postCommitDelayMs);

        ImGui::Separator();
        checkbox("Dùng timer độ chính xác cao", config.advanced.preciseTiming);
        checkbox("Ép dùng uinput thay cho forward Backspace", config.advanced.forceUinput);
        checkbox("Ép uinput Shift+Left cho trình duyệt", config.advanced.useUinputShiftSelectForBrowser);
        checkbox("Ép surrounding text v2 cho trình duyệt", config.advanced.useSurroundingV2ForBrowser);
        checkbox("Tự reset bộ gõ sau khi click chuột trong trình duyệt", config.advanced.enableMouseTracking);
        checkbox("Chuyển tiếp phím đầu để tương thích trình duyệt", config.advanced.forwardFirstCharacter);
    }

    void drawWindow(
        ConfigStore& config, const std::vector<std::string>& inputMethods, const std::vector<std::string>& charsets,
        bool& listeningShortcut, std::string& status, bool& running
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

        const float footerHeight = ImGui::GetFrameHeightWithSpacing() * 2.8F;
        static bool confirmReset = false;
        static int activeTab = 0;
        static int prevTab = 0;
        static size_t pendingDeleteIndex = SIZE_MAX;
        static ConfigStore savedConfig;
        static bool savedConfigInitialized = false;
        if (!savedConfigInitialized) {
            savedConfig = config;
            savedConfigInitialized = true;
        }
        int newActiveTab = activeTab;

        if (ImGui::BeginTabBar("SettingsTabBar", ImGuiTabBarFlags_None)) {
            if (ImGui::BeginTabItem("Bộ gõ")) {
                newActiveTab = 0;
                ImGui::BeginChild(
                    "BasicScroll", ImVec2(0.0F, -footerHeight), false, ImGuiWindowFlags_HorizontalScrollbar
                );
                drawBasic(config, inputMethods, charsets, listeningShortcut);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Macro")) {
                newActiveTab = 1;
                ImGui::BeginChild(
                    "MacroScroll", ImVec2(0.0F, -footerHeight), false, ImGuiWindowFlags_HorizontalScrollbar
                );
                drawMacros(config, pendingDeleteIndex);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Nâng cao")) {
                newActiveTab = 2;
                ImGui::BeginChild(
                    "AdvancedScroll", ImVec2(0.0F, -footerHeight), false, ImGuiWindowFlags_HorizontalScrollbar
                );
                drawAdvanced(config);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
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

        const bool fallbackDirty =
            (config.main.shiftSelectFallbackForBrowser.value()
             != savedConfig.main.shiftSelectFallbackForBrowser.value());
        const bool tab2Dirty = !(config.advanced == savedConfig.advanced) || fallbackDirty;

        const bool currentTabDirty = (activeTab == 0) ? tab0Dirty : (activeTab == 1) ? tab1Dirty : tab2Dirty;

        const areca::ArecaConfig defaultMain{};
        areca::ArecaConfig mainForDefaultCheck = config.main;
        mainForDefaultCheck.shiftSelectFallbackForBrowser.setValue(defaultMain.shiftSelectFallbackForBrowser.value());
        const bool tab0IsDefault = (mainForDefaultCheck == defaultMain);

        const bool tab1IsDefault = (config.macros == areca::MacroTableConfig{});

        const bool fallbackIsDefault =
            (config.main.shiftSelectFallbackForBrowser.value() == defaultMain.shiftSelectFallbackForBrowser.value());
        const bool tab2IsDefault = (config.advanced == areca::AdvancedConfig{}) && fallbackIsDefault;

        const bool currentTabIsDefault = (activeTab == 0) ? tab0IsDefault
            : (activeTab == 1)                            ? tab1IsDefault
                                                          : tab2IsDefault;

        ImGui::Separator();
        ImGui::BeginDisabled(!currentTabDirty);
        if (ImGui::Button("Lưu và áp dụng", ImVec2(150.0F, 0.0F))) {
            config.save();
            savedConfig = config;
            std::string reloadError;
            status = reloadArecaAddon(reloadError) ? "Đã lưu và Areca đang chạy đã tải lại cấu hình."
                                                   : "Đã lưu file, nhưng Areca chưa áp dụng: " + reloadError;
            confirmReset = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Huỷ các thay đổi")) {
            if (activeTab == 0) {
                const bool currentFallback = config.main.shiftSelectFallbackForBrowser.value();
                config.main = savedConfig.main;
                config.main.shiftSelectFallbackForBrowser.setValue(currentFallback);
                listeningShortcut = false;
            } else if (activeTab == 1) {
                config.macros = savedConfig.macros;
                pendingDeleteIndex = SIZE_MAX;
            } else {
                config.advanced = savedConfig.advanced;
                config.main.shiftSelectFallbackForBrowser.setValue(
                    savedConfig.main.shiftSelectFallbackForBrowser.value()
                );
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
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7F, 0.2F, 0.2F, 1.0F));
            if (ImGui::Button("Xác nhận khôi phục?")) {
                if (activeTab == 0) {
                    const bool currentFallback = config.main.shiftSelectFallbackForBrowser.value();
                    config.main = areca::ArecaConfig{};
                    config.main.shiftSelectFallbackForBrowser.setValue(currentFallback);
                    listeningShortcut = false;
                } else if (activeTab == 1) {
                    config.macros = areca::MacroTableConfig{};
                } else {
                    config.advanced = areca::AdvancedConfig{};
                    config.main.shiftSelectFallbackForBrowser.setValue(
                        areca::ArecaConfig{}.shiftSelectFallbackForBrowser.value()
                    );
                }
                config.save();
                savedConfig = config;
                std::string reloadError;
                status = reloadArecaAddon(reloadError)
                    ? "Đã khôi phục và áp dụng."
                    : "Đã khôi phục và lưu, nhưng Areca chưa áp dụng: " + reloadError;
                confirmReset = false;
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::Button("Hủy")) {
                confirmReset = false;
            }
        }
        const float exitButtonWidth = 90.0F;
        ImGui::SameLine(ImGui::GetWindowWidth() - exitButtonWidth - ImGui::GetStyle().WindowPadding.x);
        if (ImGui::Button("Thoát", ImVec2(exitButtonWidth, 0.0F))) {
            running = false;
        }
        if (!status.empty()) {
            ImGui::TextWrapped("%s", status.c_str());
        }
        ImGui::End();
    }

} // namespace areca::settings
