#pragma once

#include <string>
#include <utility>
#include <vector>

#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>
#include <fcitx-config/option.h>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>

namespace areca {

enum class PresentationMode { Rewrite, Preedit, Redirect };
FCITX_CONFIG_ENUM_NAME_WITH_I18N(PresentationMode, N_("Rewrite trực tiếp"),
                                 N_("Preedit"), N_("Redirect (EN)"));

enum class SpellcheckMode { Off, Basic, Realtime };
FCITX_CONFIG_ENUM_NAME_WITH_I18N(SpellcheckMode, N_("Không kiểm tra (Tắt)"),
                                 N_("Khôi phục từ sau khi gõ xong"),
                                 N_("Khôi phục từ ngay trong lúc gõ"));

enum class MacroBypassModifier { Disabled, Shift, Control, Alt, Super };
FCITX_CONFIG_ENUM_NAME_WITH_I18N(MacroBypassModifier, N_("Tắt"), N_("Shift"),
                                 N_("Ctrl"), N_("Alt"), N_("Super"));

inline bool isMacroBypassSpace(const fcitx::Key &key,
                               MacroBypassModifier modifier) {
  const auto normalized = key.normalize();
  if (normalized.sym() != FcitxKey_space &&
      normalized.sym() != FcitxKey_KP_Space) {
    return false;
  }
  const auto states = normalized.states();
  const bool shift = states.test(fcitx::KeyState::Shift);
  const bool control = states.test(fcitx::KeyState::Ctrl);
  const bool alt = states.test(fcitx::KeyState::Alt);
  const bool super = states.test(fcitx::KeyState::Super) ||
                     states.test(fcitx::KeyState::Meta) ||
                     states.test(fcitx::KeyState::Hyper) ||
                     states.test(fcitx::KeyState::Super2) ||
                     states.test(fcitx::KeyState::Hyper2);
  switch (modifier) {
  case MacroBypassModifier::Shift:
    return shift && !control && !alt && !super;
  case MacroBypassModifier::Control:
    return control && !shift && !alt && !super;
  case MacroBypassModifier::Alt:
    return alt && !shift && !control && !super;
  case MacroBypassModifier::Super:
    return super && !shift && !control && !alt;
  case MacroBypassModifier::Disabled:
    return false;
  }
  return false;
}

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

enum class AppBackendMode {
  Auto = 0,
  SurroundingText = 1,
  ShiftSelect = 2,
  Backspace = 3,
  Preedit = 4,
  Redirect = 5,
  // Xoá và gõ lại bằng phím ảo Wayland (zwp_virtual_keyboard_v1).
  VirtualKeyboard = 6,
  // Không còn trong menu; giữ để cấu hình cũ vẫn đọc được và được coi là
  // Tự động. Native giờ là tuỳ chọn nâng cao thay uinput.
  Native = 7,
  // Backspace như trước khi có marker: xoá đúng N, không commit "·".
  BackspaceUnmarked = 8
};
FCITX_CONFIG_ENUM_NAME_WITH_I18N(AppBackendMode, N_("Tự động"),
                                 N_("Surrounding"), N_("Shift select"),
                                 N_("Backspace"), N_("Preedit"),
                                 N_("Redirect (EN)"), N_("Virtual keyboard"),
                                 N_("Native"), N_("Backspace unmarked"));

inline PresentationMode
presentationModeForAppSetting(AppBackendMode mode, PresentationMode fallback) {
  switch (mode) {
  case AppBackendMode::Preedit:
    return PresentationMode::Preedit;
  case AppBackendMode::Redirect:
    return PresentationMode::Redirect;
  case AppBackendMode::SurroundingText:
  case AppBackendMode::ShiftSelect:
  case AppBackendMode::Backspace:
  case AppBackendMode::VirtualKeyboard:
  case AppBackendMode::Native:
  case AppBackendMode::BackspaceUnmarked:
    return PresentationMode::Rewrite;
  case AppBackendMode::Auto:
    return fallback;
  }
  return fallback;
}

FCITX_CONFIGURATION(
    AppBackendOverrideEntry,
    fcitx::Option<std::string> appName{this, "AppName", N_("Tên ứng dụng"), ""};
    fcitx::Option<std::string> frontend{this, "Frontend", N_("Frontend"), ""};
    fcitx::OptionWithAnnotation<AppBackendMode, fcitx::EnumAnnotation> mode{
        this, "Mode", N_("Chế độ / backend"), AppBackendMode::Auto};);

FCITX_CONFIGURATION(MacroEntry,
                    fcitx::Option<std::string> key{this, "Key",
                                                   N_("Từ viết tắt"), ""};
                    fcitx::Option<std::string> value{
                        this, "Value", N_("Nội dung thay thế"), ""};);

FCITX_CONFIGURATION(
    AppProfilesConfig,
    fcitx::OptionWithAnnotation<std::vector<AppBackendOverrideEntry>,
                                fcitx::ListDisplayOptionAnnotation>
        appOverrides{this,
                     "AppOverrides",
                     N_("Cấu hình chế độ / backend theo ứng dụng"),
                     {},
                     {},
                     {},
                     fcitx::ListDisplayOptionAnnotation("AppName")};);

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
    fcitx::HiddenOption<int, fcitx::IntConstrain> uinputBackspaceAckTimeoutMs{
        this, "UinputBackspaceAckTimeoutMs",
        N_("Timeout ACK uinput Backspace (ms)"), 150,
        fcitx::IntConstrain(1, 5000)};
    fcitx::Option<int, fcitx::IntConstrain> afterBackspaceWaitMs{
        this, "AfterBackspaceWaitMs", N_("Chờ sau Backspace cuối (ms)"), 10,
        fcitx::IntConstrain(0, 5000)};
    fcitx::Option<int, fcitx::IntConstrain> waylandAfterBackspaceWaitMs{
        this, "WaylandAfterBackspaceWaitMs",
        N_("Chờ sau Backspace cuối Wayland (ms)"), 10,
        fcitx::IntConstrain(0, 5000)};
    fcitx::Option<int, fcitx::IntConstrain> ximAfterBackspaceWaitMs{
        this, "XimAfterBackspaceWaitMs", N_("Chờ sau Backspace cuối XIM (ms)"),
        10, fcitx::IntConstrain(0, 5000)};
    fcitx::Option<int, fcitx::IntConstrain> fcitx4AfterBackspaceWaitMs{
        this, "Fcitx4AfterBackspaceWaitMs",
        N_("Chờ sau Backspace cuối Fcitx4 (ms)"), 10,
        fcitx::IntConstrain(0, 5000)};
    fcitx::Option<int, fcitx::IntConstrain> dbusAfterBackspaceWaitMs{
        this, "DbusAfterBackspaceWaitMs",
        N_("Chờ sau Backspace cuối DBus (ms)"), 10,
        fcitx::IntConstrain(0, 5000)};
    fcitx::Option<int, fcitx::IntConstrain> ibusAfterBackspaceWaitMs{
        this, "IBusAfterBackspaceWaitMs",
        N_("Chờ sau Backspace cuối IBus (ms)"), 10,
        fcitx::IntConstrain(0, 5000)};
    // Chờ marker "·" tới ứng dụng trước khi backend phát phím xoá. Ẩn khỏi
    // giao diện cài đặt; vẫn sửa được bằng tay trong areca-advanced.conf.
    fcitx::HiddenOption<int, fcitx::IntConstrain> afterMarkerCommitWaitMs{
        this, "AfterMarkerCommitWaitMs",
        N_("Chờ sau marker trước khi xoá (ms)"), 10,
        fcitx::IntConstrain(0, 5000)};
    fcitx::HiddenOption<int, fcitx::IntConstrain>
        waylandAfterMarkerCommitWaitMs{
            this, "WaylandAfterMarkerCommitWaitMs",
            N_("Chờ sau marker trước khi xoá Wayland (ms)"), 10,
            fcitx::IntConstrain(0, 5000)};
    fcitx::HiddenOption<int, fcitx::IntConstrain> ximAfterMarkerCommitWaitMs{
        this, "XimAfterMarkerCommitWaitMs",
        N_("Chờ sau marker trước khi xoá XIM (ms)"), 10,
        fcitx::IntConstrain(0, 5000)};
    fcitx::HiddenOption<int, fcitx::IntConstrain> fcitx4AfterMarkerCommitWaitMs{
        this, "Fcitx4AfterMarkerCommitWaitMs",
        N_("Chờ sau marker trước khi xoá Fcitx4 (ms)"), 10,
        fcitx::IntConstrain(0, 5000)};
    fcitx::HiddenOption<int, fcitx::IntConstrain> dbusAfterMarkerCommitWaitMs{
        this, "DbusAfterMarkerCommitWaitMs",
        N_("Chờ sau marker trước khi xoá DBus (ms)"), 10,
        fcitx::IntConstrain(0, 5000)};
    fcitx::HiddenOption<int, fcitx::IntConstrain> ibusAfterMarkerCommitWaitMs{
        this, "IBusAfterMarkerCommitWaitMs",
        N_("Chờ sau marker trước khi xoá IBus (ms)"), 10,
        fcitx::IntConstrain(0, 5000)};
    fcitx::Option<int, fcitx::IntConstrain> uinputShiftSelectDelayMs{
        this, "UinputShiftSelectDelayMs",
        N_("Delay giữa các phím uinput Shift+Left (ms)"), 1,
        fcitx::IntConstrain(0, 1000)};
    fcitx::HiddenOption<int, fcitx::IntConstrain> uinputShiftSelectAckTimeoutMs{
        this, "UinputShiftSelectAckTimeoutMs",
        N_("Timeout ACK uinput Shift+Left (ms)"), 150,
        fcitx::IntConstrain(1, 5000)};
    fcitx::Option<int, fcitx::IntConstrain> afterUinputShiftSelectWaitMs{
        this, "AfterUinputShiftSelectWaitMs",
        N_("Chờ sau khi thả Shift uinput (ms)"), 5,
        fcitx::IntConstrain(0, 5000)};
    fcitx::Option<int, fcitx::IntConstrain> waylandAfterUinputShiftSelectWaitMs{
        this, "WaylandAfterUinputShiftSelectWaitMs",
        N_("Chờ sau khi thả Shift uinput Wayland (ms)"), 5,
        fcitx::IntConstrain(0, 5000)};
    fcitx::Option<int, fcitx::IntConstrain> ximAfterUinputShiftSelectWaitMs{
        this, "XimAfterUinputShiftSelectWaitMs",
        N_("Chờ sau khi thả Shift uinput XIM (ms)"), 5,
        fcitx::IntConstrain(0, 5000)};
    fcitx::Option<int, fcitx::IntConstrain> fcitx4AfterUinputShiftSelectWaitMs{
        this, "Fcitx4AfterUinputShiftSelectWaitMs",
        N_("Chờ sau khi thả Shift uinput Fcitx4 (ms)"), 5,
        fcitx::IntConstrain(0, 5000)};
    fcitx::Option<int, fcitx::IntConstrain> dbusAfterUinputShiftSelectWaitMs{
        this, "DbusAfterUinputShiftSelectWaitMs",
        N_("Chờ sau khi thả Shift uinput DBus (ms)"), 5,
        fcitx::IntConstrain(0, 5000)};
    fcitx::Option<int, fcitx::IntConstrain> ibusAfterUinputShiftSelectWaitMs{
        this, "IBusAfterUinputShiftSelectWaitMs",
        N_("Chờ sau khi thả Shift uinput IBus (ms)"), 5,
        fcitx::IntConstrain(0, 5000)};
    fcitx::Option<int, fcitx::IntConstrain> surroundingWaitMs{
        this, "SurroundingWaitMs", N_("Chờ sau xóa surrounding text (ms)"), 3,
        fcitx::IntConstrain(0, 5000)};
    fcitx::HiddenOption<int, fcitx::IntConstrain> surroundingProbeTimeoutMs{
        this, "SurroundingProbeTimeoutMs",
        N_("Timeout kiểm tra surrounding text (ms)"), 80,
        fcitx::IntConstrain(1, 1000)};
    // Khoá ini giữ tên từ thời còn XTest để không mất cấu hình đã lưu; các
    // option này giờ là thời gian của Native (libei).
    fcitx::Option<int, fcitx::IntConstrain> nativeBackspaceDelayMs{
        this, "XTestBackspaceDelayMs",
        N_("Delay giữa các Backspace Native (ms)"), 1,
        fcitx::IntConstrain(0, 1000)};
    fcitx::Option<int, fcitx::IntConstrain> afterNativeBackspaceWaitMs{
        this, "AfterXTestBackspaceWaitMs",
        N_("Chờ sau Backspace cuối Native (ms)"), 10,
        fcitx::IntConstrain(0, 5000)};
    fcitx::Option<int, fcitx::IntConstrain> waylandAfterNativeBackspaceWaitMs{
        this, "WaylandAfterXTestBackspaceWaitMs",
        N_("Chờ sau Backspace cuối Native Wayland (ms)"), 10,
        fcitx::IntConstrain(0, 5000)};
    fcitx::Option<int, fcitx::IntConstrain> ximAfterNativeBackspaceWaitMs{
        this, "XimAfterXTestBackspaceWaitMs",
        N_("Chờ sau Backspace cuối Native XIM (ms)"), 10,
        fcitx::IntConstrain(0, 5000)};
    fcitx::Option<int, fcitx::IntConstrain> fcitx4AfterNativeBackspaceWaitMs{
        this, "Fcitx4AfterXTestBackspaceWaitMs",
        N_("Chờ sau Backspace cuối Native Fcitx4 (ms)"), 10,
        fcitx::IntConstrain(0, 5000)};
    fcitx::Option<int, fcitx::IntConstrain> dbusAfterNativeBackspaceWaitMs{
        this, "DbusAfterXTestBackspaceWaitMs",
        N_("Chờ sau Backspace cuối Native DBus (ms)"), 10,
        fcitx::IntConstrain(0, 5000)};
    fcitx::Option<int, fcitx::IntConstrain> ibusAfterNativeBackspaceWaitMs{
        this, "IBusAfterXTestBackspaceWaitMs",
        N_("Chờ sau Backspace cuối Native IBus (ms)"), 10,
        fcitx::IntConstrain(0, 5000)};
    fcitx::Option<int, fcitx::IntConstrain> postCommitDelayMs{
        this, "PostCommitDelayMs", N_("Delay sau mỗi commit (ms)"), 20,
        fcitx::IntConstrain(0, 5000)};
    fcitx::Option<bool> useUinputInsteadOfForwardKey{
        this, "UseUinputInsteadOfForwardKey",
        N_("Dùng uinput thay cho ForwardKey"), true};
    // Tên khoá cũ từ thời còn XTest, giữ để không mất cấu hình đã lưu.
    fcitx::Option<bool> useNativeInsteadOfUinput{
        this, "UseXTestInsteadOfUinput",
        N_("Dùng Native (libei) thay thế uinput"), false};
    fcitx::Option<bool> enableMouseTracking{
        this, "EnableMouseTracking", N_("Tự reset bộ gõ sau khi click chuột"),
        true};
    // Ẩn khỏi giao diện cài đặt; vẫn tắt được bằng tay trong
    // areca-advanced.conf nếu một ứng dụng gặp lỗi.
    fcitx::HiddenOption<bool> forwardFirstCharacter{
        this, "ForwardFirstCharacter", N_("Chuyển tiếp phím đầu"), true};);

FCITX_CONFIGURATION(
    ArecaConfig,
    fcitx::HiddenOption<PresentationMode> presentationMode{
        this, "PresentationMode", N_("Chế độ hiển thị"),
        PresentationMode::Rewrite};
    fcitx::HiddenOption<fcitx::KeyList,
                        fcitx::ListConstrain<fcitx::KeyConstrain>>
        switchModeKey{this,
                      "SwitchModeKey",
                      N_("Phím tắt chuyển chế độ gõ"),
                      {},
                      fcitx::KeyListConstrain(
                          fcitx::KeyConstrainFlag::AllowModifierLess)};
    fcitx::KeyListOption selectBackendKey{
        this,
        "SelectBackendKey",
        N_("Phím tắt chọn chế độ / backend cho ứng dụng"),
        {fcitx::Key("grave")},
        fcitx::KeyListConstrain(fcitx::KeyConstrainFlag::AllowModifierLess)};
    fcitx::HiddenOption<std::string> trayIcon{this, "TrayIcon",
                                              N_("Biểu tượng khay hệ thống"),
                                              "org.fcitx.Fcitx5.fcitx-areca"};
    fcitx::OptionWithAnnotation<std::string, StringListAnnotation>
        bambooInputMethod{this, "BambooInputMethod", N_("Kiểu gõ Bamboo"),
                          "Telex 2"};
    fcitx::OptionWithAnnotation<std::string, StringListAnnotation>
        outputCharset{this, "OutputCharset", N_("Bảng mã đầu ra"), "Unicode"};
    fcitx::OptionWithAnnotation<SpellcheckMode, fcitx::EnumAnnotation>
        spellcheckMode{this, "SpellcheckMode", N_("Chế độ kiểm tra chính tả"),
                       SpellcheckMode::Realtime};
    fcitx::Option<bool> modernStyle{
        this, "ModernStyle", N_("Đặt dấu kiểu oà, uý thay cho òa, úy"), true};
    fcitx::Option<bool> autoCapitalizeAfterPunctuation{
        this, "AutoCapitalizeAfterPunctuation",
        N_("Tự viết hoa sau dấu kết câu (. ! ?)"), false};
    fcitx::Option<bool> emojiSuggestion{this, "EmojiSuggestion",
                                        N_("Gợi ý emoji khi gõ :từ"), false};
    fcitx::Option<bool> enableMacro{this, "EnableMacro", N_("Bật macro"), true};
    fcitx::Option<bool> capitalizeMacro{
        this, "CapitalizeMacro", N_("Tự đổi hoa/thường cho nội dung macro"),
        true};
    fcitx::Option<bool> revertMacroOnBackspace{
        this, "RevertMacroOnBackspace", N_("Hoàn tác macro khi nhấn Backspace"),
        true};
    fcitx::OptionWithAnnotation<MacroBypassModifier, fcitx::EnumAnnotation>
        macroBypassModifier{this, "MacroBypassModifier",
                            N_("Phím giữ để tạm bỏ qua macro khi nhấn Space"),
                            MacroBypassModifier::Shift};
    fcitx::SubConfigOption macroEditor{this, "MacroEditor",
                                       N_("Chỉnh sửa macro"),
                                       "fcitx://config/addon/areca/macro"};
    fcitx::SubConfigOption appOverridesEditor{
        this, "AppOverridesEditor", N_("Cấu hình backend theo ứng dụng"),
        "fcitx://config/addon/areca/apps"};
    fcitx::Option<bool> backspaceRecovery{
        this, "BackspaceRecovery",
        N_("Khôi phục lỗi chính tả khi nhấn Backspace"), true};
    fcitx::Option<bool> restoreSurroundingText{
        this, "RestoreSurroundingText", N_("Cho phép sửa từ đã commit"), false};
    fcitx::Option<bool> disableVietnameseInTerminal{
        this, "DisableVietnameseInTerminal",
        N_("Tắt tiếng Việt trong terminal"), false};
    fcitx::SubConfigOption advancedEditor{
        this, "AdvancedEditor", N_("Cấu hình nâng cao"),
        "fcitx://config/addon/areca/advanced"};
    fcitx::Option<bool> debug{this, "Debug", N_("Bật log debug Areca"),
                              false};);

} // namespace areca
