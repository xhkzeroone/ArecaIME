#pragma once

#include <string>
#include <utility>
#include <vector>

#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>
#include <fcitx-config/option.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/key.h>

namespace areca {

enum class PresentationMode { Rewrite, Preedit, Redirect };
FCITX_CONFIG_ENUM_NAME_WITH_I18N(PresentationMode, N_("Rewrite trực tiếp"),
                                 N_("Preedit"), N_("Redirect (EN)"));

enum class SpellcheckMode { Off, Basic, Realtime };
FCITX_CONFIG_ENUM_NAME_WITH_I18N(SpellcheckMode,
                                 N_("Không kiểm tra (Tắt)"),
                                 N_("Khôi phục từ sau khi gõ xong"),
                                 N_("Khôi phục từ ngay trong lúc gõ"));

struct StringListAnnotation : public fcitx::EnumAnnotation {
  void setList(std::vector<std::string> list) { list_ = std::move(list); }

  void dumpDescription(fcitx::RawConfig &config) const {
    fcitx::EnumAnnotation::dumpDescription(config);
    for (size_t i = 0; i < list_.size(); ++i) {
      config.setValueByPath("Enum/" + std::to_string(i), list_[i]);
    }
  }

private:
  std::vector<std::string> list_;
};

FCITX_CONFIGURATION(MacroEntry,
                    fcitx::Option<std::string> key{this, "Key",
                                                   N_("Từ viết tắt"), ""};
                    fcitx::Option<std::string> value{
                        this, "Value", N_("Nội dung thay thế"), ""};);

FCITX_CONFIGURATION(
    MacroTableConfig,
    fcitx::OptionWithAnnotation<std::vector<MacroEntry>,
                                fcitx::ListDisplayOptionAnnotation>
        macros{this,
               "Macro",
               N_("Danh sách macro"),
               {},
               {},
               {},
               fcitx::ListDisplayOptionAnnotation("Key")};);

FCITX_CONFIGURATION(
    AdvancedConfig,
    fcitx::Option<int, fcitx::IntConstrain> backspaceDelayMs{
        this, "BackspaceDelayMs", N_("Delay giữa các Backspace (ms)"), 1,
        fcitx::IntConstrain(0, 1000)};
    fcitx::Option<int, fcitx::IntConstrain> afterBackspaceWaitMs{
        this, "AfterBackspaceWaitMs",
        N_("Chờ sau Backspace cuối (ms)"), 10,
        fcitx::IntConstrain(0, 5000)};
    fcitx::Option<int, fcitx::IntConstrain> waylandAfterBackspaceWaitMs{
        this, "WaylandAfterBackspaceWaitMs",
        N_("Chờ sau Backspace cuối Wayland (ms)"),
        3,
        fcitx::IntConstrain(0, 5000)};
    fcitx::Option<int, fcitx::IntConstrain> ximAfterBackspaceWaitMs{
        this, "XimAfterBackspaceWaitMs",
        N_("Chờ sau Backspace cuối XIM (ms)"),
        10,
        fcitx::IntConstrain(0, 5000)};
    fcitx::Option<int, fcitx::IntConstrain> fcitx4AfterBackspaceWaitMs{
        this, "Fcitx4AfterBackspaceWaitMs",
        N_("Chờ sau Backspace cuối Fcitx4 (ms)"),
        10,
        fcitx::IntConstrain(0, 5000)};
    fcitx::Option<int, fcitx::IntConstrain> dbusAfterBackspaceWaitMs{
        this, "DbusAfterBackspaceWaitMs",
        N_("Chờ sau Backspace cuối DBus (ms)"),
        5,
        fcitx::IntConstrain(0, 5000)};
    fcitx::Option<int, fcitx::IntConstrain> postCommitDelayMs{
        this, "PostCommitDelayMs", N_("Delay sau mỗi commit (ms)"), 20,
        fcitx::IntConstrain(0, 5000)};
    fcitx::Option<bool> preciseTiming{this, "PreciseTiming",
                                      N_("Dùng timer độ chính xác cao"), true};
    fcitx::Option<bool> forceUinput{
        this, "ForceUinput", N_("Ép dùng uinput thay cho forward Backspace"),
        false};
);

FCITX_CONFIGURATION(
    ArecaConfig,
    fcitx::OptionWithAnnotation<PresentationMode,
                                PresentationModeI18NAnnotation>
        presentationMode{this, "PresentationMode", N_("Chế độ hiển thị"),
                         PresentationMode::Rewrite};
    fcitx::KeyListOption switchModeKey{
        this,
        "SwitchModeKey",
        N_("Phím tắt chuyển chế độ gõ"),
        {fcitx::Key("Alt+space")},
        fcitx::KeyListConstrain(fcitx::KeyConstrainFlag::AllowModifierLess)};
    fcitx::HiddenOption<int, fcitx::IntConstrain> legacyBackspaceDelayMs{
        this, "BackspaceDelayMs", N_("Delay giữa các Backspace (ms)"), 1,
        fcitx::IntConstrain(0, 1000)};
    fcitx::HiddenOption<int, fcitx::IntConstrain> legacyAfterBackspaceWaitMs{
        this, "AfterBackspaceWaitMs",
        N_("Thời gian chờ sau Backspace cuối (ms)"), 10,
        fcitx::IntConstrain(0, 5000)};
    fcitx::HiddenOption<int, fcitx::IntConstrain> legacyPostCommitDelayMs{
        this, "PostCommitDelayMs", N_("Delay sau mỗi commit (ms)"), 20,
        fcitx::IntConstrain(0, 5000)};
    fcitx::OptionWithAnnotation<std::string, StringListAnnotation>
        bambooInputMethod{this, "BambooInputMethod", N_("Kiểu gõ Bamboo"),
                          "Telex 2"};
    fcitx::OptionWithAnnotation<std::string, StringListAnnotation>
        outputCharset{this, "OutputCharset", N_("Bảng mã đầu ra"), "Unicode"};
    fcitx::OptionWithAnnotation<SpellcheckMode, fcitx::EnumAnnotation>
        spellcheckMode{this, "SpellcheckMode", N_("Chế độ kiểm tra chính tả"),
                       SpellcheckMode::Realtime};
    fcitx::Option<bool> backspaceRecovery{
        this, "BackspaceRecovery",
        N_("Khôi phục lỗi chính tả khi nhấn Backspace"), true};
    fcitx::Option<bool> modernStyle{
        this, "ModernStyle", N_("Đặt dấu kiểu oà, uý thay cho òa, úy"), true};
    fcitx::Option<bool> autoCapitalizeAfterPunctuation{
        this, "AutoCapitalizeAfterPunctuation",
        N_("Tự viết hoa sau dấu kết câu (. ! ?)"), false};
    fcitx::Option<bool> enableMacro{this, "EnableMacro", N_("Bật macro"), true};
    fcitx::Option<bool> capitalizeMacro{
        this, "CapitalizeMacro", N_("Tự đổi hoa/thường cho nội dung macro"),
        true};
    fcitx::SubConfigOption macroEditor{this, "MacroEditor",
                                       N_("Chỉnh sửa macro"),
                                       "fcitx://config/addon/areca/macro"};
    fcitx::SubConfigOption advancedEditor{
        this, "AdvancedEditor", N_("Cấu hình nâng cao"),
        "fcitx://config/addon/areca/advanced"};
    fcitx::Option<bool> debug{this, "Debug", N_("Bật log debug Areca"), false};);

} // namespace areca
