#!/usr/bin/env python3
"""Standalone GTK4 settings UI for Areca / Fcitx5."""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import gi

gi.require_version("Gtk", "4.0")
gi.require_version("Gdk", "4.0")
from gi.repository import Gdk, Gio, GLib, Gtk

from icon_library import (
    APP_ID, DEFAULT_TRAY_ICON, ORIGINAL_ICON, PRESETS, atomic_write,
    desktop_entry, icon_button, prepare_icon, refresh_launcher_name, ui_icon,
)


CONFIG_DIR = Path(os.environ.get("XDG_CONFIG_HOME") or Path.home() / ".config") / "fcitx5" / "conf"
APP_VERSION = "v8.3.12"  # Update manually when preparing a release.
MAIN_FILE = "areca.conf"
MACRO_FILE = "areca-macro-table.conf"
APPS_FILE = "areca-apps.conf"
ADVANCED_FILE = "areca-advanced.conf"

# Colors mirror settings/ui_theme.cpp: background, sidebar, surface, text, accent.
THEMES = {
    "System": ("Theo hệ thống", None),
    "Light": ("Light", ('#f7f5ed', '#eceadd', '#fffef8', '#10211c', '#0b5f3a')),
    "MacOS": ("macOS", ('#f5f5f7', '#e8e8ed', '#ffffff', '#1d1d1f', '#007aff')),
    "Dark": ("Dark", ('#121216', '#0c0c10', '#1e1f26', '#dcdee9', '#82aaff')),
    "ArecaDark": ("Areca Dark", ('#121a17', '#0e1512', '#1c2722', '#d2e4db', '#38c878')),
    "Gruvbox": ("Gruvbox", ('#282828', '#1d2021', '#3c3836', '#ebdbb2', '#d79921')),
    "Catppuccin": ("Catppuccin Mocha", ('#1e1e2e', '#11111b', '#313244', '#cdd6f4', '#cba6f7')),
    "CatppuccinLatte": ("Catppuccin Latte", ('#eff1f5', '#dce0e8', '#ccd0da', '#4c4f69', '#8839ef')),
    "Dracula": ("Dracula", ('#282a36', '#181921', '#44475a', '#f8f8f2', '#bd93f9')),
    "OneDark": ("One Dark", ('#282c34', '#181b21', '#2c313c', '#abb2bf', '#61afef')),
    "TokyoNight": ("Tokyo Night", ('#1a1b2e', '#0f101a', '#24283b', '#c0caf5', '#7aa2f7')),
}

METHODS = [
    "Telex", "Telex 2", "VNI", "VIQR", "Telex + VNI", "Telex + VNI + VIQR",
    "Microsoft layout", "VNI Bàn phím tiếng Pháp",
]
CHARSETS = [
    "Unicode", "TCVN3 (ABC)", "VNI Windows", "Unicode tổ hợp", "Windows 1258 codepage",
    "VIQR", "VISCII", "VPS", "BKHCM 2", "BKHCM 1", "Vietware X", "Vietware Full",
    "UTF-8", "NCR Decimal", "NCR Hex", "Unicode C string Hex", "Unicode C string Decimal",
]
SPELLCHECK = [
    "Không kiểm tra (Tắt)",
    "Khôi phục từ sau khi gõ xong",
    "Khôi phục từ ngay trong lúc gõ",
]
MACRO_BYPASS = ["Tắt", "Shift", "Ctrl", "Alt", "Super"]
APP_MODES = ["Tự động", "Surrounding", "Shift select", "Backspace", "Preedit", "Redirect (EN)", "Virtual keyboard"]

MAIN_DEFAULTS = {
    "BambooInputMethod": "Telex 2",
    "OutputCharset": "Unicode",
    "SpellcheckMode": SPELLCHECK[2],
    "ModernStyle": True,
    "AutoCapitalizeAfterPunctuation": False,
    "EnableMacro": True,
    "CapitalizeMacro": True,
    "RevertMacroOnBackspace": True,
    "MacroBypassModifier": "Shift",
    "BackspaceRecovery": True,
    "RestoreSurroundingText": False,
    "ShiftSelectFallbackForBrowser": False,
    "UseBackspaceForUrl": True,
    "DisableVietnameseInTerminal": False,
    "Debug": False,
}
MAIN_KEY_LIST_DEFAULTS = {"SwitchModeKey": [], "SelectBackendKey": ["grave"]}

ADVANCED_DEFAULTS = {
    "BackspaceDelayMs": 1,
    "UinputBackspaceAckTimeoutMs": 150,
    "AfterBackspaceWaitMs": 10,
    "WaylandAfterBackspaceWaitMs": 10,
    "XimAfterBackspaceWaitMs": 10,
    "Fcitx4AfterBackspaceWaitMs": 10,
    "DbusAfterBackspaceWaitMs": 10,
    "IBusAfterBackspaceWaitMs": 10,
    "UinputShiftSelectDelayMs": 1,
    "UinputShiftSelectAckTimeoutMs": 150,
    "AfterUinputShiftSelectWaitMs": 10,
    "WaylandAfterUinputShiftSelectWaitMs": 5,
    "XimAfterUinputShiftSelectWaitMs": 10,
    "Fcitx4AfterUinputShiftSelectWaitMs": 10,
    "DbusAfterUinputShiftSelectWaitMs": 10,
    "IBusAfterUinputShiftSelectWaitMs": 10,
    "SurroundingWaitMs": 3,
    "SurroundingProbeTimeoutMs": 80,
    "SurroundingDeleteDelayMs": 10,
    "WaylandSurroundingDeleteDelayMs": 0,
    "AfterSurroundingDeleteWaitMs": 1,
    "XTestBackspaceDelayMs": 1,
    "AfterXTestBackspaceWaitMs": 10,
    "WaylandAfterXTestBackspaceWaitMs": 10,
    "XimAfterXTestBackspaceWaitMs": 10,
    "Fcitx4AfterXTestBackspaceWaitMs": 10,
    "DbusAfterXTestBackspaceWaitMs": 10,
    "IBusAfterXTestBackspaceWaitMs": 10,
    "PostCommitDelayMs": 20,
    "PreciseTiming": True,
    "UseSurroundingV2InsteadOfSurroundingV1": False,
    "UseUinputInsteadOfForwardKey": True,
    "UseXTestInsteadOfUinput": False,
    "EnableMouseTracking": True,
    "ForwardFirstCharacter": True,
}


def color_channels(color: str) -> tuple[float, ...]:
    return tuple(int(color[index:index + 2], 16) / 255 for index in (1, 3, 5))


def mix_color(foreground: str, background: str, amount: float) -> str:
    return "#" + "".join(
        f"{round(255 * (a * amount + b * (1 - amount))):02x}"
        for a, b in zip(color_channels(foreground), color_channels(background))
    )


def contrast_ratio(first: str, second: str) -> float:
    def luminance(color: str) -> float:
        linear = [v / 12.92 if v <= .04045 else ((v + .055) / 1.055) ** 2.4
                  for v in color_channels(color)]
        return sum(v * weight for v, weight in zip(linear, (.2126, .7152, .0722)))
    a, b = sorted((luminance(first), luminance(second)))
    return (b + .05) / (a + .05)


def readable_color(color: str, backgrounds: list[str]) -> str:
    """Keep the hue where possible, adjusting text to at least 4.5:1."""
    target = max(("#000000", "#ffffff"),
                 key=lambda c: min(contrast_ratio(c, bg) for bg in backgrounds))
    for step in range(101):
        candidate = mix_color(target, color, step / 100)
        if all(contrast_ratio(candidate, bg) >= 4.5 for bg in backgrounds):
            return candidate
    return target


def theme_text_colors(colors: tuple[str, ...]) -> tuple[str, str, str]:
    bg, sidebar, surface, fg, accent = colors
    backgrounds = [bg, sidebar, surface]
    muted = readable_color(mix_color(fg, surface, .78), backgrounds)
    accent_backgrounds = [mix_color(accent, base, opacity)
                          for base in backgrounds for opacity in (0, .08, .16, .22, .24, .32)]
    accent_text = readable_color(accent, accent_backgrounds)
    on_accent = readable_color(bg, [accent])
    return muted, accent_text, on_accent


def decode_value(value: str) -> str:
    value = value.strip()
    if len(value) >= 2 and value[0] == value[-1] == '"':
        try:
            return json.loads(value)
        except ValueError:
            return value[1:-1]
    return value


def encode_value(value: str) -> str:
    if not value or re.search(r"[\s#\"]", value):
        return json.dumps(value, ensure_ascii=False)
    return value


class ConfigDocument:
    """Small loss-minimizing editor for Fcitx's section-based INI files."""

    def __init__(self, path: Path):
        self.path = path
        try:
            self.lines = path.read_text(encoding="utf-8").splitlines()
        except FileNotFoundError:
            self.lines = []

    def get(self, section: str, key: str, default: str = "") -> str:
        current = ""
        for line in self.lines:
            match = re.match(r"^\s*\[([^]]+)\]\s*$", line)
            if match:
                current = match.group(1)
                continue
            if current == section:
                item = re.match(r"^\s*([^#;=]+?)\s*=\s*(.*?)\s*$", line)
                if item and item.group(1).strip() == key:
                    return decode_value(item.group(2))
        return default

    def set(self, section: str, key: str, value: str) -> None:
        current = ""
        section_start = None
        section_end = len(self.lines)
        for index, line in enumerate(self.lines):
            header = re.match(r"^\s*\[([^]]+)\]\s*$", line)
            if header:
                if current == section:
                    section_end = index
                    break
                current = header.group(1)
                if current == section:
                    section_start = index
                    section_end = len(self.lines)
                    continue
            if current == section:
                item = re.match(r"^(\s*)([^#;=]+?)(\s*=\s*).*$", line)
                if item and item.group(2).strip() == key:
                    self.lines[index] = f"{item.group(1)}{key}{item.group(3)}{encode_value(value)}"
                    return

        assignment = f"{key}={encode_value(value)}"
        if section_start is not None:
            self.lines.insert(section_end, assignment)
        elif section:
            if self.lines and self.lines[-1].strip():
                self.lines.append("")
            self.lines.extend([f"[{section}]", assignment])
        else:
            self.lines.append(assignment)

    def replace_indexed(self, prefix: str, entries: list[dict[str, str]]) -> None:
        updated = []
        current = ""
        skipping = False
        pattern = re.compile(rf"^{re.escape(prefix)}/\d+$")
        for line in self.lines:
            header = re.match(r"^\s*\[([^]]+)\]\s*$", line)
            if header:
                current = header.group(1)
                skipping = bool(pattern.match(current))
            if not skipping:
                updated.append(line)

        while updated and not updated[-1].strip():
            updated.pop()
        if entries:
            if updated:
                updated.append("")
            for index, entry in enumerate(entries):
                updated.append(f"[{prefix}/{index}]")
                for key, value in entry.items():
                    if value != "":
                        updated.append(f"{key}={encode_value(value)}")
                if index + 1 < len(entries):
                    updated.append("")
        self.lines = updated

    def indexed(self, prefix: str) -> list[dict[str, str]]:
        entries: dict[int, dict[str, str]] = {}
        current = None
        for line in self.lines:
            header = re.match(r"^\s*\[([^]]+)\]\s*$", line)
            if header:
                match = re.fullmatch(rf"{re.escape(prefix)}/(\d+)", header.group(1))
                current = int(match.group(1)) if match else None
                if current is not None:
                    entries[current] = {}
                continue
            if current is not None:
                item = re.match(r"^\s*([^#;=]+?)\s*=\s*(.*?)\s*$", line)
                if item:
                    entries[current][item.group(1).strip()] = decode_value(item.group(2))
        return [entries[index] for index in sorted(entries)]

    def list_values(self, section: str) -> list[str]:
        values = {}
        current = ""
        for line in self.lines:
            header = re.match(r"^\s*\[([^]]+)\]\s*$", line)
            if header:
                current = header.group(1)
                continue
            if current == section:
                item = re.match(r"^\s*(\d+)\s*=\s*(.*?)\s*$", line)
                if item:
                    values[int(item.group(1))] = decode_value(item.group(2))
        return [values[index] for index in sorted(values)]

    def replace_list_values(self, section: str, values: list[str]) -> None:
        updated = []
        current = ""
        skipping = False
        for line in self.lines:
            header = re.match(r"^\s*\[([^]]+)\]\s*$", line)
            if header:
                current = header.group(1)
                skipping = current == section
            if not skipping:
                updated.append(line)
        while updated and not updated[-1].strip():
            updated.pop()
        if values:
            if updated:
                updated.append("")
            updated.append(f"[{section}]")
            updated.extend(f"{index}={encode_value(value)}" for index, value in enumerate(values))
        self.lines = updated

    def save(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        payload = "\n".join(self.lines).rstrip() + "\n"
        fd, temporary = tempfile.mkstemp(prefix=f".{self.path.name}.", dir=self.path.parent)
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as handle:
                handle.write(payload)
                handle.flush()
                os.fsync(handle.fileno())
            os.replace(temporary, self.path)
        finally:
            if os.path.exists(temporary):
                os.unlink(temporary)


class ConfigStore:
    def __init__(self):
        self.main = ConfigDocument(CONFIG_DIR / MAIN_FILE)
        self.macros = ConfigDocument(CONFIG_DIR / MACRO_FILE)
        self.apps = ConfigDocument(CONFIG_DIR / APPS_FILE)
        self.advanced = ConfigDocument(CONFIG_DIR / ADVANCED_FILE)

    def save(self, *documents: ConfigDocument) -> tuple[bool, str]:
        for document in documents:
            document.save()
        return self.reload_fcitx()

    @staticmethod
    def reload_fcitx(timeout_ms: int = 3000) -> tuple[bool, str]:
        try:
            bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
            bus.call_sync(
                "org.fcitx.Fcitx5",
                "/controller",
                "org.fcitx.Fcitx.Controller1",
                "ReloadAddonConfig",
                GLib.Variant("(s)", ("areca",)),
                None,
                Gio.DBusCallFlags.NONE,
                timeout_ms,
                None,
            )
            result = subprocess.run(
                ["fcitx5-remote", "-r"],
                check=False,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=max(1, timeout_ms / 1000),
            )
            if result.returncode != 0:
                return False, f"fcitx5-remote thoát với mã {result.returncode}"
            return True, ""
        except GLib.Error as error:
            return False, error.message or str(error)
        except FileNotFoundError:
            return False, "không tìm thấy lệnh fcitx5-remote"
        except subprocess.TimeoutExpired:
            return False, "fcitx5-remote không phản hồi khi nạp lại"

    @staticmethod
    def restart_fcitx() -> tuple[bool, str]:
        try:
            result = subprocess.run(
                ["fcitx5-remote", "-r"],
                check=False,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=5,
            )
            if result.returncode != 0:
                return False, f"fcitx5-remote thoát với mã {result.returncode}"
            return True, ""
        except FileNotFoundError:
            return False, "không tìm thấy lệnh fcitx5-remote"
        except subprocess.TimeoutExpired:
            return False, "fcitx5-remote không phản hồi khi nạp lại"


def bool_value(value: str, default: bool = False) -> bool:
    if not value:
        return default
    return value.lower() in {"true", "1", "yes", "on"}


def enum_index(value: str, choices: list[str], default: int = 0) -> int:
    try:
        return choices.index(value)
    except ValueError:
        return default


class SettingsWindow(Gtk.ApplicationWindow):
    PAGES = [
        ("overview", "Tổng quan", "home"),
        ("typing", "Bộ gõ", "keyboard"),
        ("macros", "Macro", "macro"),
        ("apps", "Ứng dụng", "apps"),
        ("advanced", "Nâng cao", "advanced"),
        ("appearance", "Giao diện", "palette"),
        ("icons", "Biểu tượng", "icons"),
        ("about", "Giới thiệu", "about"),
    ]

    def __init__(self, application: Gtk.Application):
        super().__init__(application=application, title="Areca Settings")
        refresh_launcher_name()
        self.set_decorated(False)
        self.set_default_size(1024, 720)
        self.set_size_request(860, 500)
        self.store = ConfigStore()
        self.appearance = ConfigDocument(CONFIG_DIR / "areca-gtk-settings.conf")
        saved_theme = self.appearance.get("Appearance", "Theme", "Light")
        self.theme = saved_theme if saved_theme in THEMES else "Light"
        saved_radius = self.appearance.get("Appearance", "WindowRadius", "24")
        self.window_radius = saved_radius if saved_radius in {"0", "5", "15", "24"} else "24"
        self.theme_buttons: dict[str, Gtk.ToggleButton] = {}
        self.radius_dropdown: Gtk.DropDown | None = None
        self.icon_buttons: dict[str, Gtk.ToggleButton] = {}
        self.icon_preview: Gtk.Image | None = None
        self.icon_label: Gtk.Label | None = None
        self.navigation: dict[str, Gtk.ToggleButton] = {}
        self.pages: dict[str, Gtk.Widget] = {}
        self.advanced_spins: dict[str, Gtk.SpinButton] = {}
        self.shortcut_capture: tuple[Gtk.Entry, Gtk.Button, Gtk.EventControllerKey] | None = None
        self._applying_preset = False
        self._toast_timeout_id = 0
        self.status_label = Gtk.Label(xalign=0)
        self.status_label.add_css_class("dim-label")
        self._install_css()
        self._build_layout()
        self.show_page("overview")
        self._refresh_icon_previews()

    def _install_css(self) -> None:
        provider = Gtk.CssProvider()
        provider.load_from_data(self._css())
        self.css_provider = provider
        Gtk.StyleContext.add_provider_for_display(
            self.get_display(), provider, Gtk.STYLE_PROVIDER_PRIORITY_USER
        )
        settings = Gtk.Settings.get_default()
        if settings:
            self._set_system_color_mode(settings)

    @staticmethod
    def _set_system_color_mode(settings: Gtk.Settings) -> None:
        if settings.find_property("gtk-interface-color-scheme"):
            settings.set_property("gtk-interface-color-scheme", Gtk.InterfaceColorScheme.DEFAULT)

    def _css(self) -> bytes:
        palette = {
            "BG": "@window_bg_color", "SIDEBAR": "@sidebar_bg_color",
            "FG": "@theme_fg_color", "MUTED": "alpha(@theme_fg_color, .78)",
            "BORDER": "@borders", "FIELD": "@theme_base_color", "SURFACE": "@theme_base_color",
            "HOVER": "@theme_fg_color", "ACCENT": "@accent_color",
            "ACCENT_TEXT": "@accent_color",
            "RADIUS": f"{self.window_radius}px",
        }
        definitions = ""
        colors = THEMES[self.theme][1]
        if colors:
            bg, sidebar, surface, fg, accent = colors
            muted, accent_text, on_accent = theme_text_colors(colors)
            palette["MUTED"] = muted
            palette["ACCENT_TEXT"] = accent_text
            light = self.theme in {"Light", "CatppuccinLatte"}
            error = "#b3261e" if light else "#ff8e99"
            for name, value in {
                "window_bg_color": bg, "sidebar_bg_color": sidebar,
                "theme_bg_color": bg, "theme_fg_color": fg,
                "theme_text_color": fg, "theme_base_color": surface,
                "borders": f"alpha({fg}, .24)", "accent_color": accent,
                "accent_bg_color": accent, "accent_fg_color": on_accent,
                "error_color": error, "error_bg_color": error,
                "error_fg_color": readable_color(bg, [error]),
                "theme_selected_bg_color": accent, "theme_selected_fg_color": on_accent,
            }.items():
                definitions += f"@define-color {name} {value};\n"
            definitions += f"@define-color theme_unfocused_fg_color {fg};\n"
        css = """
        window, .areca-content { background: BG; color: FG; }
        window { border-radius: RADIUS; }
        window:backdrop { background: BG; color: FG; }
        window.message .dialog-vbox { padding: 20px 24px; }
        window.message .dialog-action-box { margin-top: 16px; }
        window.message .dialog-action-area button { padding: 8px 16px; margin: 0 4px; border-radius: 6px; }
        .areca-sidebar { background: SIDEBAR; color: FG; }
        .content-split > separator { background: alpha(FG, .12); min-width: 1px; }
        .areca-brand { padding: 22px 18px 20px; }
        .window-drag-bar { min-height: 44px; background: BG; }
        .areca-brand-title { font-size: 19px; font-weight: 700; }
        .areca-brand-subtitle { color: MUTED; font-size: 12px; }
        button.areca-nav, button.areca-nav:checked { margin: 2px 10px; padding: 9px 10px; border: 1px solid transparent; border-radius: 6px; box-shadow: none; background: transparent; color: FG; }
        button.areca-nav:hover { background: alpha(HOVER, .06); }
        button.areca-nav:checked { background: alpha(ACCENT, .16); color: ACCENT_TEXT; }
        button.areca-nav:checked:hover { background: alpha(ACCENT, .22); }
        .areca-content { padding: 28px 28px 32px; }
        .page-title { font-size: 26px; font-weight: 700; }
        .page-description { color: MUTED; margin-top: 6px; margin-bottom: 8px; }
        .section-title { color: FG; font-size: 14px; font-weight: 700; margin: 24px 0 10px; }
        .setting-row { padding: 12px 14px; border-bottom: 1px solid alpha(BORDER, .55); }
        .setting-row:last-child { border-bottom: none; }
        .setting-value, .areca-content .dim-label { color: MUTED; opacity: 1; }
        .areca-content entry text, .areca-content spinbutton text { color: FG; caret-color: FG; }
        .areca-content text > placeholder { color: MUTED; opacity: 1; }
        .areca-content text selection { background: @accent_bg_color; color: @accent_fg_color; }
        .areca-content button label, .areca-content button image,
        .areca-sidebar button label, .areca-sidebar button image,
        .status-line button label, .status-line button image,
        window.message button label, window.message button image { color: inherit; }
        .areca-content spinbutton button image { color: FG; }
        .save-toast label { color: @accent_fg_color; }
        .save-toast.error label { color: @error_fg_color; }
        tooltip, tooltip label { background: BG; color: FG; }
        .list-group { padding: 12px; }
        .list-toolbar { margin-bottom: 12px; }
        .setting-title { font-weight: 600; }
        .setting-description { color: MUTED; font-size: 12px; }
        .status-line { padding: 8px 14px; border-top: 1px solid alpha(BORDER, .7); color: FG; }
        .status-line > label { color: MUTED; opacity: 1; }
        .status-line button { color: FG; box-shadow: none; }
        .status-line button.restore-default { background: alpha(ACCENT, .08); color: ACCENT_TEXT; border: 1px solid alpha(ACCENT, .25); }
        .status-line button.restore-default:hover { background: alpha(ACCENT, .16); border-color: alpha(ACCENT, .45); }
        .save-toast { padding: 10px 16px; border-radius: 7px; background: @accent_bg_color; color: @accent_fg_color; }
        .save-toast.error { background: @error_bg_color; color: @error_fg_color; }
        .areca-content label.macro-count-error { color: @error_color; background: transparent; }
        .settings-group { background: SURFACE; border: 1px solid alpha(BORDER, .65); border-radius: 8px; }
        .areca-content entry, .areca-content spinbutton, .areca-content dropdown > button { color: FG; background: FIELD; border: 1px solid alpha(FG, .18); border-radius: 5px; box-shadow: none; }
        .areca-content entry { padding: 8px 12px; min-height: 20px; }
        .areca-content button, .status-line button { padding: 8px 12px; min-height: 20px; }
        .areca-content dropdown > button { padding: 8px 12px; }
        /* The desktop theme may color the selected item's intermediate widgets.
           Set the closed dropdown's text explicitly instead of inheriting it. */
        .areca-content dropdown > button label,
        .areca-content dropdown > button image { color: FG; opacity: 1; }
        .areca-content spinbutton { padding: 0; min-height: 0; }
        .areca-content spinbutton text { padding: 8px 12px; min-height: 20px; }
        .areca-content spinbutton button { padding: 8px; min-width: 16px; }
        .areca-content spinbutton text, .areca-content spinbutton button { color: FG; background: transparent; border-color: transparent; box-shadow: none; }
        .areca-content entry:focus-within, .areca-content spinbutton:focus-within, .areca-content dropdown > button:focus-visible { outline: 2px solid alpha(ACCENT, .65); outline-offset: 2px; }
        .areca-content button, .status-line button, window.message button { border-radius: 6px; color: FG; background: alpha(FG, .05); border: 1px solid alpha(FG, .14); box-shadow: none; }
        .areca-content button:hover, .status-line button:hover, window.message button:hover { background: alpha(FG, .10); border-color: alpha(FG, .24); }
        .areca-content button:active, .status-line button:active, window.message button:active { background: alpha(FG, .16); }
        .areca-content button.suggested-action { background: alpha(ACCENT, .16); color: ACCENT_TEXT; border-color: alpha(ACCENT, .35); font-weight: 600; }
        .areca-content button.suggested-action:hover { background: alpha(ACCENT, .24); border-color: alpha(ACCENT, .55); }
        .areca-content button.suggested-action:active { background: alpha(ACCENT, .32); }
        .areca-content button.flat, .status-line button.flat { background: transparent; border-color: transparent; }
        .areca-content button.flat:hover, .status-line button.flat:hover { background: alpha(FG, .08); }
        .areca-content button.remove-action:hover, window.message .dialog-action-area button:last-child { background: alpha(@error_color, .12); color: @error_color; border-color: alpha(@error_color, .30); }
        window.message .dialog-action-area button:last-child:hover { background: alpha(@error_color, .20); border-color: alpha(@error_color, .50); }
        .areca-content button:focus-visible, .status-line button:focus-visible, window.message button:focus-visible { outline: 2px solid alpha(ACCENT, .65); outline-offset: 2px; }
        .areca-content button:disabled, .status-line button:disabled { opacity: .5; }
        switch { background: alpha(FG, .12); border: 1px solid alpha(FG, .18); border-radius: 999px; }
        switch slider { background: FIELD; border: 1px solid alpha(FG, .14); border-radius: 999px; }
        switch:checked { background: ACCENT; border-color: ACCENT; }
        .theme-grid > flowboxchild { padding: 4px; }
        .areca-content button.theme-choice { padding: 14px; }
        .areca-content button.theme-choice:checked { background: alpha(ACCENT, .16); border-color: ACCENT; }
        .areca-content button.theme-choice:checked:hover { background: alpha(ACCENT, .24); }
        .icon-preview { padding: 8px; background: alpha(FG, .04); border: 1px solid alpha(FG, .12); border-radius: 12px; }
        .icon-grid > flowboxchild { padding: 4px; }
        .areca-content button.icon-choice { padding: 12px 8px; }
        .areca-content button.icon-choice:checked { background: alpha(ACCENT, .16); border-color: ACCENT; }
        .theme-swatch { min-width: 32px; min-height: 20px; border-radius: 4px; border: 1px solid alpha(FG, .18); }
        popover > contents { background: FIELD; color: FG; }
        popover listview { background: FIELD; color: FG; }
        popover row { background: FIELD; color: FG; }
        popover row:hover { background: mix(FIELD, FG, .08); color: FG; }
        popover row:selected { background: @accent_bg_color; color: @accent_fg_color; }
        popover row label, popover row image { color: inherit; }
        .macro-row, .app-row { padding: 8px; border: 1px solid alpha(BORDER, .75); border-radius: 6px; }
        """
        css += """
        .areca-brand-title:backdrop, .page-title:backdrop,
        .section-title:backdrop, .setting-title:backdrop { color: FG; }
        """
        if self.theme == "MacOS":
            css += """
            .areca-sidebar { background: alpha(SIDEBAR, .88); }
            .areca-content .settings-group { border-radius: 12px; }
            .areca-content button, .status-line button,
            .areca-content entry, .areca-content dropdown > button { border-radius: 9px; }
            """
        for token in sorted(palette, key=len, reverse=True):
            css = css.replace(token, palette[token])
        for key, (_, colors) in THEMES.items():
            swatches = colors if colors else ("@window_bg_color", "@sidebar_bg_color", "@theme_base_color", "@theme_fg_color", "@accent_color")
            for index, color in enumerate(swatches):
                css += f".swatch-{key}-{index} {{ background: {color}; }}\n"
        return (definitions + css).encode("utf-8")

    def _build_layout(self) -> None:
        self.content_paned = Gtk.Paned(orientation=Gtk.Orientation.HORIZONTAL)
        self.content_paned.add_css_class("content-split")
        self.content_paned.set_wide_handle(True)
        self.content_paned.set_resize_start_child(False)
        self.content_paned.set_shrink_start_child(False)
        self.content_paned.set_resize_end_child(True)
        self.content_paned.set_position(260)
        self.set_child(self.content_paned)

        sidebar = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        sidebar.add_css_class("areca-sidebar")
        sidebar.set_size_request(250, -1)
        self.content_paned.set_start_child(sidebar)

        brand = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=11)
        brand.add_css_class("areca-brand")
        logo_path = self._selected_icon_path("Settings")
        logo = Gtk.Image.new_from_file(str(logo_path))
        logo.set_pixel_size(40)
        logo.set_size_request(40, 40)
        logo.set_tooltip_text("Areca")
        self.brand_logo = logo
        brand.append(logo)
        brand_text = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=1)
        name = Gtk.Label(label="Areca Settings", xalign=0)
        name.add_css_class("areca-brand-title")
        subtitle = Gtk.Label(label="Bộ gõ tiếng Việt cho Linux", xalign=0)
        subtitle.add_css_class("areca-brand-subtitle")
        brand_text.append(name)
        brand_text.append(subtitle)
        brand.append(brand_text)
        brand.set_tooltip_text("Kéo vùng này để di chuyển cửa sổ")
        gesture = Gtk.GestureClick()
        gesture.set_button(1)
        gesture.connect("pressed", self._begin_window_move)
        brand.add_controller(gesture)
        self.header_drag_gesture = gesture
        sidebar.append(brand)

        navigation = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        navigation.set_margin_top(6)
        for page_id, title, icon_name in self.PAGES:
            button = Gtk.ToggleButton()
            button.add_css_class("areca-nav")
            button.set_hexpand(True)
            row = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=11)
            row.append(ui_icon(icon_name))
            row.append(Gtk.Label(label=title, xalign=0, hexpand=True))
            button.set_child(row)
            button.connect("toggled", self._on_nav_toggled, page_id)
            self.navigation[page_id] = button
            navigation.append(button)
        sidebar.append(navigation)
        sidebar.append(Gtk.Box(vexpand=True))
        footer = Gtk.Label(label="ARECA  ·  FCITX5", xalign=0)
        footer.add_css_class("areca-brand-subtitle")
        footer.set_margin_start(19)
        footer.set_margin_bottom(16)
        sidebar.append(footer)

        main = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=0)
        main.set_hexpand(True)
        main.set_vexpand(True)
        self.content_paned.set_end_child(main)
        drag_bar = Gtk.CenterBox()
        drag_bar.add_css_class("window-drag-bar")
        drag_bar.set_tooltip_text("Kéo thanh này để di chuyển cửa sổ")
        drag_gesture = Gtk.GestureClick()
        drag_gesture.set_button(1)
        drag_gesture.connect("pressed", self._begin_window_move)
        drag_bar.add_controller(drag_gesture)
        self.content_drag_bar = drag_bar
        main.append(drag_bar)
        self.stack = Gtk.Stack()
        self.stack.set_transition_type(Gtk.StackTransitionType.CROSSFADE)
        self.stack.set_transition_duration(140)
        self.stack.set_hexpand(True)
        self.stack.set_vexpand(True)
        self.pages = {
            "overview": self._build_overview(), "typing": self._build_typing(),
            "macros": self._build_macros(), "apps": self._build_apps(),
            "advanced": self._build_advanced(), "appearance": self._build_appearance(),
            "icons": self._build_icons(), "about": self._build_about(),
        }
        for page_id, page in self.pages.items():
            self.stack.add_named(page, page_id)
        content_overlay = Gtk.Overlay()
        content_overlay.set_child(self.stack)
        self.toast_revealer = Gtk.Revealer()
        self.toast_revealer.set_transition_type(Gtk.RevealerTransitionType.CROSSFADE)
        self.toast_revealer.set_transition_duration(140)
        self.toast_revealer.set_halign(Gtk.Align.END)
        self.toast_revealer.set_valign(Gtk.Align.START)
        self.toast_revealer.set_margin_top(16)
        self.toast_revealer.set_margin_end(20)
        self.toast_box = Gtk.Box()
        self.toast_box.add_css_class("save-toast")
        self.toast_label = Gtk.Label(xalign=0)
        self.toast_box.append(self.toast_label)
        self.toast_revealer.set_child(self.toast_box)
        content_overlay.add_overlay(self.toast_revealer)
        main.append(content_overlay)
        status_bar = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        status_bar.add_css_class("status-line")
        self.status_label.set_hexpand(True)
        status_bar.append(self.status_label)
        self.restore_button = Gtk.Button(label="Khôi phục mặc định")
        self.restore_button.add_css_class("restore-default")
        self.restore_button.connect("clicked", self._restore_defaults)
        status_bar.append(self.restore_button)
        self.close_button = Gtk.Button()
        close_content = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=7)
        close_content.append(ui_icon("close"))
        close_content.append(Gtk.Label(label="Đóng"))
        self.close_button.set_child(close_content)
        self.close_button.set_tooltip_text("Đóng cửa sổ cài đặt")
        self.close_button.add_css_class("flat")
        self.close_button.connect("clicked", lambda *_: self.close())
        status_bar.append(self.close_button)
        main.append(status_bar)
        self.active_page = "overview"

    def _begin_window_move(self, gesture: Gtk.GestureClick, _press_count: int, _x: float, _y: float) -> None:
        event = gesture.get_current_event()
        surface = self.get_surface()
        if event is None or surface is None or not isinstance(surface, Gdk.Toplevel):
            return
        has_position, x, y = event.get_position()
        device = event.get_device()
        if has_position and device is not None:
            surface.begin_move(device, gesture.get_current_button(), x, y, event.get_time())

    def _page(self, title: str, description: str) -> tuple[Gtk.Box, Gtk.Box]:
        scroller = Gtk.ScrolledWindow()
        scroller.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        scroller.set_hexpand(True)
        scroller.set_vexpand(True)
        content = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=0)
        content.add_css_class("areca-content")
        scroller.set_child(content)
        if title:
            heading = Gtk.Label(label=title, xalign=0)
            heading.add_css_class("page-title")
            content.append(heading)
        if description:
            intro = Gtk.Label(label=description, xalign=0, wrap=True)
            intro.add_css_class("page-description")
            content.append(intro)
        return scroller, content

    def _section(self, parent: Gtk.Box, title: str) -> Gtk.Box:
        heading = Gtk.Label(label=title, xalign=0)
        heading.add_css_class("section-title")
        parent.append(heading)
        group = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=0)
        group.add_css_class("settings-group")
        parent.append(group)
        return group

    def _row(self, group: Gtk.Box, title: str, description: str = "") -> Gtk.Box:
        row = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=16)
        row.add_css_class("setting-row")
        text = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=3)
        text.set_hexpand(True)
        text.set_valign(Gtk.Align.CENTER)
        label = Gtk.Label(label=title, xalign=0, wrap=True)
        label.add_css_class("setting-title")
        text.append(label)
        if description:
            detail = Gtk.Label(label=description, xalign=0, wrap=True)
            detail.add_css_class("setting-description")
            text.append(detail)
        row.append(text)
        group.append(row)
        return row

    def _switch(self, group: Gtk.Box, title: str, key: str, description: str = "") -> None:
        row = self._row(group, title, description)
        control = Gtk.Switch()
        control.set_valign(Gtk.Align.CENTER)
        default = MAIN_DEFAULTS.get(key, False)
        value = self.store.main.get("", key, str(default))
        control.set_active(bool_value(value, default))
        control.connect("notify::active", self._on_switch, self.store.main, key)
        row.append(control)

    def _dropdown(
        self, group: Gtk.Box, title: str, document: ConfigDocument, key: str, choices: list[str],
        section: str = "", description: str = "", default: int = 0,
    ) -> Gtk.DropDown:
        row = self._row(group, title, description)
        current = document.get(section, key)
        display_choices = list(choices)
        if current and current not in display_choices:
            display_choices.insert(0, current)
        control = Gtk.DropDown.new_from_strings(display_choices)
        control.set_selected(enum_index(current, display_choices, default))
        control.set_size_request(205, -1)
        control.set_valign(Gtk.Align.CENTER)
        control.connect("notify::selected", self._on_dropdown, document, section, key, display_choices)
        row.append(control)
        return control

    def _spin(self, group: Gtk.Box, title: str, key: str, maximum: int, description: str = "") -> None:
        row = self._row(group, title, description)
        try:
            default = ADVANCED_DEFAULTS.get(key, 0)
            current = int(self.store.advanced.get("", key, str(default)))
        except ValueError:
            current = 0
        control = Gtk.SpinButton.new_with_range(0, maximum, 1)
        control.set_value(max(0, min(maximum, current)))
        control.set_valign(Gtk.Align.CENTER)
        control.set_width_chars(5)
        control.connect("value-changed", self._on_spin, key)
        self.advanced_spins[key] = control
        row.append(control)

    def _build_overview(self) -> Gtk.Widget:
        page, content = self._page("Tổng quan", "Cấu hình bộ gõ Areca đang được dùng bởi Fcitx5.")
        summary = self._section(content, "Cấu hình hiện tại")
        method = self.store.main.get("", "BambooInputMethod", "Telex 2")
        charset = self.store.main.get("", "OutputCharset", "Unicode")
        spellcheck = self.store.main.get("", "SpellcheckMode", SPELLCHECK[2])
        self._info_row(summary, "Kiểu gõ", method)
        self._info_row(summary, "Bảng mã", charset)
        self._info_row(summary, "Chính tả", spellcheck)
        shortcuts = self._section(content, "Phím tắt")
        self._info_row(shortcuts, "Chọn backend ứng dụng", self._key_list("SelectBackendKey"))
        action = Gtk.Button(label="Mở cài đặt bộ gõ")
        action.set_halign(Gtk.Align.START)
        action.set_margin_top(12)
        action.add_css_class("suggested-action")
        action.connect("clicked", lambda *_: self.show_page("typing"))
        content.append(action)
        return page

    def _info_row(self, group: Gtk.Box, title: str, value: str) -> None:
        row = self._row(group, title)
        label = Gtk.Label(label=value or "Chưa gán", xalign=1, wrap=True)
        label.add_css_class("setting-value")
        label.set_valign(Gtk.Align.CENTER)
        row.append(label)

    def _key_list(self, section: str) -> str:
        values = self.store.main.list_values(section)
        if not values:
            values = MAIN_KEY_LIST_DEFAULTS.get(section, [])
        return ", ".join(values) or "Chưa gán"

    def _build_typing(self) -> Gtk.Widget:
        page, content = self._page("Bộ gõ", "Chọn kiểu nhập, bảng mã và các hành vi khi gõ tiếng Việt.")
        group = self._section(content, "Phương thức nhập")
        dropdowns = Gtk.SizeGroup.new(Gtk.SizeGroupMode.HORIZONTAL)
        method_dropdown = self._dropdown(
            group, "Kiểu gõ", self.store.main, "BambooInputMethod", METHODS,
            description="Bộ gõ Bamboo tích hợp trong Areca.", default=METHODS.index(MAIN_DEFAULTS["BambooInputMethod"]),
        )
        charset_dropdown = self._dropdown(
            group, "Bảng mã đầu ra", self.store.main, "OutputCharset", CHARSETS,
            default=CHARSETS.index(MAIN_DEFAULTS["OutputCharset"]),
        )
        spellcheck_dropdown = self._dropdown(
            group, "Kiểm tra chính tả", self.store.main, "SpellcheckMode", SPELLCHECK,
            default=SPELLCHECK.index(MAIN_DEFAULTS["SpellcheckMode"]),
        )
        for control in (method_dropdown, charset_dropdown, spellcheck_dropdown):
            dropdowns.add_widget(control)
            control.set_halign(Gtk.Align.END)
        self.typing_dropdown_size_group = dropdowns
        behavior = self._section(content, "Hành vi")
        for title, key, description in [
            ("Đặt dấu kiểu mới (oà, uý)", "ModernStyle", "Tắt để dùng kiểu dấu cũ (òa, úy)."),
            ("Tự viết hoa sau dấu kết câu", "AutoCapitalizeAfterPunctuation", ". ! ?"),
            ("Bật khôi phục chính tả khi nhấn Backspace", "BackspaceRecovery", "Khôi phục từ vừa sửa khi xoá ngược."),
            ("Cho phép sửa từ đã commit", "RestoreSurroundingText", "Sửa từ vừa gõ dựa trên văn bản xung quanh con trỏ."),
            ("Shift+Left fallback cho trình duyệt", "ShiftSelectFallbackForBrowser", "Dùng khi trình duyệt không hỗ trợ surrounding text."),
            ("Xử lý riêng ô URL và thanh địa chỉ", "UseBackspaceForUrl", "Chọn backend tương thích cho ô địa chỉ."),
            ("Tắt tiếng Việt trong terminal", "DisableVietnameseInTerminal", "Chuyển tiếp nguyên phím khi đang ở terminal, giống ô mật khẩu."),
        ]:
            self._switch(behavior, title, key, description)
        diagnostics = self._section(content, "Chẩn đoán")
        self._switch(diagnostics, "Bật log debug Areca", "Debug", "Log được ghi trong journal của Fcitx5.")
        return page

    def _build_macros(self) -> Gtk.Widget:
        page, content = self._page("Macro", "Mở rộng từ viết tắt thành nội dung thường dùng.")
        behavior = self._section(content, "Hành vi macro")
        self._switch(behavior, "Bật macro", "EnableMacro")
        self._switch(behavior, "Tự đổi hoa/thường nội dung macro", "CapitalizeMacro")
        self._switch(behavior, "Hoàn tác macro khi nhấn Backspace", "RevertMacroOnBackspace")
        self._dropdown(
            behavior, "Phím giữ để bỏ qua macro", self.store.main, "MacroBypassModifier", MACRO_BYPASS,
            description="Giữ phím đã chọn rồi nhấn Space.",
            default=MACRO_BYPASS.index(MAIN_DEFAULTS["MacroBypassModifier"]),
        )

        section = self._section(content, "Danh sách macro")
        section.add_css_class("list-group")
        toolbar = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        toolbar.add_css_class("list-toolbar")
        add_button = Gtk.Button()
        add_button.add_css_class("suggested-action")
        add_contents = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=7)
        add_contents.append(ui_icon("plus"))
        add_contents.append(Gtk.Label(label="Thêm macro"))
        add_button.set_child(add_contents)
        add_button.connect("clicked", self._add_macro, section)
        toolbar.append(add_button)
        self.macro_count_label = Gtk.Label(xalign=1, hexpand=True)
        self.macro_count_label.add_css_class("dim-label")
        toolbar.append(self.macro_count_label)
        section.append(toolbar)
        self.macro_rows = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        section.append(self.macro_rows)
        self.macro_row_extras: dict[Gtk.Widget, dict[str, str]] = {}
        self._refresh_macro_rows()
        return page

    def _refresh_macro_rows(self) -> None:
        while child := self.macro_rows.get_first_child():
            self.macro_rows.remove(child)
        self.macro_row_extras = {}
        for entry in self.store.macros.indexed("Macro"):
            extras = {key: value for key, value in entry.items() if key not in {"Key", "Value"}}
            self.macro_rows.append(
                self._macro_row(entry.get("Key", ""), entry.get("Value", ""), extras)
            )
        self._update_macro_count()

    def _macro_row(
        self, key: str, value: str, extras: dict[str, str] | None = None
    ) -> Gtk.Widget:
        row = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        row.add_css_class("macro-row")
        self.macro_row_extras[row] = dict(extras or {})
        key_entry = Gtk.Entry(placeholder_text="Từ viết tắt", hexpand=True)
        value_entry = Gtk.Entry(placeholder_text="Nội dung thay thế", hexpand=True)
        key_entry.set_text(key)
        value_entry.set_text(value)
        row.append(key_entry)
        row.append(value_entry)
        remove = icon_button("delete", "Xóa")
        remove.set_tooltip_text("Xóa macro")
        remove.add_css_class("flat")
        remove.add_css_class("remove-action")
        remove.connect("clicked", self._remove_macro, row)
        row.append(remove)

        def persist(*_args) -> None:
            self._persist_macros()

        key_entry.connect("activate", persist)
        value_entry.connect("activate", persist)
        key_entry.connect("notify::has-focus", lambda widget, _pspec: persist() if not widget.has_focus() else None)
        value_entry.connect("notify::has-focus", lambda widget, _pspec: persist() if not widget.has_focus() else None)
        key_entry.connect("changed", lambda *_args: self._update_macro_count())
        value_entry.connect("changed", lambda *_args: self._update_macro_count())
        return row

    def _add_macro(self, _button: Gtk.Button, _section: Gtk.Box) -> None:
        self.macro_rows.append(self._macro_row("", ""))
        self._update_macro_count()
        self._saved("Điền đủ từ viết tắt và nội dung rồi rời ô để lưu")

    def _remove_macro(self, _button: Gtk.Button, row: Gtk.Widget) -> None:
        key = row.get_first_child().get_text()
        dialog = Gtk.AlertDialog()
        dialog.set_message("Xóa macro này?")
        dialog.set_detail(f"Từ viết tắt: {key}" if key else "Dòng macro chưa hoàn chỉnh.")
        dialog.set_buttons(["Hủy", "Xóa"])
        dialog.set_cancel_button(0)
        dialog.set_default_button(0)
        dialog.choose(self, None, self._confirm_remove_macro, row)

    def _confirm_remove_macro(
        self, dialog: Gtk.AlertDialog, result: Gio.AsyncResult, row: Gtk.Widget
    ) -> None:
        try:
            response = dialog.choose_finish(result)
        except GLib.Error:
            return
        if response != 1:
            return
        self.macro_rows.remove(row)
        self.macro_row_extras.pop(row, None)
        self._update_macro_count()
        self._persist_macros()

    def _update_macro_count(self) -> int:
        total = 0
        invalid = 0
        child = self.macro_rows.get_first_child()
        while child:
            key_entry = child.get_first_child()
            value_entry = key_entry.get_next_sibling()
            total += 1
            if not key_entry.get_text().strip() or not value_entry.get_text().strip():
                invalid += 1
            child = child.get_next_sibling()
        if invalid:
            self.macro_count_label.set_text(f"{total} macro · {invalid} chưa hoàn chỉnh")
            self.macro_count_label.add_css_class("macro-count-error")
        else:
            self.macro_count_label.set_text(f"{total} macro")
            self.macro_count_label.remove_css_class("macro-count-error")
        return invalid

    def _persist_macros(self) -> bool:
        entries = []
        child = self.macro_rows.get_first_child()
        while child:
            key_entry = child.get_first_child()
            value_entry = key_entry.get_next_sibling()
            key = key_entry.get_text()
            value = value_entry.get_text()
            if not key.strip() or not value.strip():
                invalid = self._update_macro_count()
                self._saved(f"Chưa lưu: hãy điền đủ từ viết tắt và nội dung cho {invalid} macro.")
                return False
            entry = dict(self.macro_row_extras.get(child, {}))
            entry.update({"Key": key, "Value": value})
            entries.append(entry)
            child = child.get_next_sibling()
        self._update_macro_count()
        self.store.macros.replace_indexed("Macro", entries)
        self._save_and_report("Đã lưu macro", self.store.macros)
        return True

    def _build_apps(self) -> Gtk.Widget:
        page, content = self._page("Ứng dụng", "Gán chế độ nhập hoặc backend Rewrite riêng cho từng ứng dụng.")
        shortcut = self._section(content, "Phím tắt")
        row = self._row(shortcut, "Mở danh sách backend", "Phím tắt dùng trong ứng dụng đang hoạt động.")
        entry = Gtk.Entry(hexpand=True)
        entry.set_valign(Gtk.Align.CENTER)
        entry.set_text(self._key_list("SelectBackendKey"))
        entry.set_placeholder_text("grave, Ctrl+space...")
        entry.connect("activate", self._save_key_list, "SelectBackendKey")
        entry.connect("notify::has-focus", self._save_key_list, "SelectBackendKey")
        row.append(entry)
        capture = Gtk.Button(label="Ghi phím")
        capture.set_valign(Gtk.Align.CENTER)
        capture.set_tooltip_text("Bấm để ghi lại tổ hợp phím trực tiếp")
        capture.connect("clicked", self._start_shortcut_capture, entry)
        row.append(capture)
        group = self._section(content, "Cấu hình theo ứng dụng")
        group.add_css_class("list-group")
        toolbar = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        toolbar.add_css_class("list-toolbar")
        add = Gtk.Button(label="Thêm ứng dụng")
        add.add_css_class("suggested-action")
        add.connect("clicked", self._add_app)
        toolbar.append(add)
        group.append(toolbar)
        self.app_rows = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        group.append(self.app_rows)
        self._refresh_app_rows()
        return page

    def _refresh_app_rows(self) -> None:
        while child := self.app_rows.get_first_child():
            self.app_rows.remove(child)
        for entry in self.store.apps.indexed("AppOverrides"):
            self.app_rows.append(self._app_row(entry))

    def _app_row(self, entry: dict[str, str]) -> Gtk.Widget:
        row = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        row.add_css_class("app-row")
        name = Gtk.Entry(placeholder_text="Tên ứng dụng (vd: code-oss)", hexpand=True)
        name.set_text(entry.get("AppName", ""))
        frontend = Gtk.Label(label=entry.get("Frontend", ""), width_chars=8)
        frontend.add_css_class("dim-label")
        mode = entry.get("Mode", "Tự động")
        mode_dropdown = Gtk.DropDown.new_from_strings(APP_MODES)
        mode_dropdown.set_selected(enum_index(mode, APP_MODES))
        mode_dropdown.set_size_request(155, -1)
        remove = icon_button("delete", "Xóa")
        remove.set_tooltip_text("Xóa cấu hình ứng dụng")
        remove.add_css_class("flat")
        remove.add_css_class("remove-action")
        row.append(name)
        row.append(frontend)
        row.append(mode_dropdown)
        row.append(remove)

        def persist(*_args) -> None:
            self._persist_apps()

        name.connect("activate", persist)
        name.connect("notify::has-focus", lambda widget, _pspec: persist() if not widget.has_focus() else None)
        mode_dropdown.connect("notify::selected", persist)
        remove.connect("clicked", self._remove_app, row)
        return row

    def _persist_apps(self) -> None:
        entries = []
        row = self.app_rows.get_first_child()
        while row:
            name = row.get_first_child()
            frontend = name.get_next_sibling()
            mode = frontend.get_next_sibling()
            app_name = name.get_text().strip()
            if app_name:
                entries.append({
                    "AppName": app_name,
                    "Frontend": frontend.get_text(),
                    "Mode": APP_MODES[mode.get_selected()],
                })
            row = row.get_next_sibling()
        self.store.apps.replace_indexed("AppOverrides", entries)
        self._save_and_report("Đã lưu cấu hình ứng dụng", self.store.apps)

    def _add_app(self, _button: Gtk.Button) -> None:
        self.app_rows.append(self._app_row({"Mode": "Tự động"}))

    def _remove_app(self, _button: Gtk.Button, row: Gtk.Widget) -> None:
        self.app_rows.remove(row)
        self._persist_apps()

    def _save_key_list(self, entry: Gtk.Entry, *args) -> None:
        section = args[-1]
        if len(args) > 1 and entry.has_focus():
            return
        values = [part.strip() for part in entry.get_text().split(",") if part.strip()]
        self.store.main.replace_list_values(section, values)
        self._save_and_report("Đã lưu phím tắt", self.store.main)

    def _start_shortcut_capture(self, button: Gtk.Button, entry: Gtk.Entry) -> None:
        if self.shortcut_capture:
            old_entry, old_button, old_controller = self.shortcut_capture
            old_entry.remove_controller(old_controller)
            old_button.set_label("Ghi phím")
            self.shortcut_capture = None
            if old_button is button:
                return

        button.set_label("Bấm tổ hợp…")
        controller = Gtk.EventControllerKey()
        controller.set_propagation_phase(Gtk.PropagationPhase.CAPTURE)
        controller.connect("key-pressed", self._capture_shortcut_key, entry, button)
        entry.add_controller(controller)
        self.shortcut_capture = (entry, button, controller)
        entry.grab_focus()

    def _capture_shortcut_key(self, _controller, keyval: int, _keycode: int,
                              state: Gdk.ModifierType, entry: Gtk.Entry,
                              button: Gtk.Button) -> bool:
        if keyval == Gdk.KEY_Escape:
            entry.remove_controller(_controller)
            button.set_label("Ghi phím")
            self.shortcut_capture = None
            return True

        modifier_names = []
        for mask, name in (
            (Gdk.ModifierType.CONTROL_MASK, "Ctrl"),
            (Gdk.ModifierType.ALT_MASK, "Alt"),
            (Gdk.ModifierType.SHIFT_MASK, "Shift"),
            (Gdk.ModifierType.SUPER_MASK, "Super"),
        ):
            if state & mask:
                modifier_names.append(name)

        # Modifier-only presses wait for the actual key in the combination.
        if keyval in (Gdk.KEY_Control_L, Gdk.KEY_Control_R, Gdk.KEY_Alt_L,
                      Gdk.KEY_Alt_R, Gdk.KEY_Shift_L, Gdk.KEY_Shift_R,
                      Gdk.KEY_Super_L, Gdk.KEY_Super_R, Gdk.KEY_Meta_L,
                      Gdk.KEY_Meta_R):
            return True

        key_name = Gdk.keyval_name(keyval)
        if not key_name:
            return True
        key_name = key_name.lower() if len(key_name) == 1 else key_name
        shortcut = "+".join([*modifier_names, key_name])
        entry.set_text(shortcut)
        self.store.main.replace_list_values("SelectBackendKey", [shortcut])
        self._save_and_report("Đã lưu phím tắt", self.store.main)
        entry.remove_controller(_controller)
        button.set_label("Ghi phím")
        self.shortcut_capture = None
        return True

    def _build_advanced(self) -> Gtk.Widget:
        page, content = self._page("Nâng cao", "Các giá trị timing và tương thích. Chỉ đổi khi ứng dụng gặp lỗi nhập liệu.")
        groups = [
            ("Backspace", [("Delay giữa Backspace (ms)", "BackspaceDelayMs", 1000), ("Chờ sau Backspace (ms)", "AfterBackspaceWaitMs", 5000), ("Wayland", "WaylandAfterBackspaceWaitMs", 5000), ("XIM", "XimAfterBackspaceWaitMs", 5000), ("Fcitx4", "Fcitx4AfterBackspaceWaitMs", 5000), ("DBus", "DbusAfterBackspaceWaitMs", 5000), ("IBus", "IBusAfterBackspaceWaitMs", 5000)]),
            ("Shift+Left", [("Delay giữa các phím (ms)", "UinputShiftSelectDelayMs", 1000), ("Chờ sau khi thả Shift (ms)", "AfterUinputShiftSelectWaitMs", 5000), ("Wayland", "WaylandAfterUinputShiftSelectWaitMs", 5000), ("XIM", "XimAfterUinputShiftSelectWaitMs", 5000), ("Fcitx4", "Fcitx4AfterUinputShiftSelectWaitMs", 5000), ("DBus", "DbusAfterUinputShiftSelectWaitMs", 5000), ("IBus", "IBusAfterUinputShiftSelectWaitMs", 5000)]),
            ("Surrounding text", [("Chờ sau xóa surrounding (ms)", "SurroundingWaitMs", 5000), ("Delay xóa surrounding v2 (ms)", "SurroundingDeleteDelayMs", 1000), ("Delay xóa v2 trên Wayland (ms)", "WaylandSurroundingDeleteDelayMs", 1000), ("Chờ sau lệnh xóa cuối (ms)", "AfterSurroundingDeleteWaitMs", 5000)]),
            ("Native Backspace", [("Delay giữa Backspace Native (ms)", "XTestBackspaceDelayMs", 1000), ("Chờ sau Backspace Native (ms)", "AfterXTestBackspaceWaitMs", 5000), ("Wayland", "WaylandAfterXTestBackspaceWaitMs", 5000), ("XIM", "XimAfterXTestBackspaceWaitMs", 5000), ("Fcitx4", "Fcitx4AfterXTestBackspaceWaitMs", 5000), ("DBus", "DbusAfterXTestBackspaceWaitMs", 5000), ("IBus", "IBusAfterXTestBackspaceWaitMs", 5000)]),
        ]
        for title, values in groups:
            group = self._section(content, title)
            for label, key, maximum in values:
                self._spin(group, label, key, maximum)
        general = self._section(content, "Chung")
        self._spin(general, "Delay sau mỗi commit (ms)", "PostCommitDelayMs", 5000)
        compat = self._section(content, "Tương thích")
        for title, key in [
            ("Dùng timer độ chính xác cao", "PreciseTiming"),
            ("Dùng Surrounding v2 thay v1", "UseSurroundingV2InsteadOfSurroundingV1"),
            ("Dùng uinput thay ForwardKey", "UseUinputInsteadOfForwardKey"),
            ("Dùng Native (libei) thay uinput", "UseXTestInsteadOfUinput"),
            ("Reset bộ gõ sau khi click chuột", "EnableMouseTracking"),
            ("Chuyển tiếp phím đầu cho trình duyệt", "ForwardFirstCharacter"),
        ]:
            self._switch_doc(compat, title, self.store.advanced, key)
        preset = Gtk.Button(label="Áp dụng preset hiệu năng")
        preset.set_margin_top(12)
        preset.add_css_class("suggested-action")
        preset.set_halign(Gtk.Align.START)
        preset.connect("clicked", self._performance_preset)
        content.append(preset)
        return page

    def _switch_doc(self, group: Gtk.Box, title: str, document: ConfigDocument, key: str) -> None:
        row = self._row(group, title)
        control = Gtk.Switch()
        control.set_valign(Gtk.Align.CENTER)
        default = ADVANCED_DEFAULTS.get(key, False)
        value = document.get("", key, str(default))
        control.set_active(bool_value(value, default))
        control.connect("notify::active", self._on_switch, document, key)
        row.append(control)

    def _performance_preset(self, _button: Gtk.Button) -> None:
        preset_values = {
            "WaylandAfterBackspaceWaitMs": 3,
            "DbusAfterBackspaceWaitMs": 5,
            "IBusAfterBackspaceWaitMs": 5,
            "WaylandAfterUinputShiftSelectWaitMs": 3,
            "DbusAfterUinputShiftSelectWaitMs": 5,
            "IBusAfterUinputShiftSelectWaitMs": 5,
            "SurroundingWaitMs": 1,
            "SurroundingDeleteDelayMs": 1,
            "WaylandSurroundingDeleteDelayMs": 1,
            "AfterSurroundingDeleteWaitMs": 1,
            "WaylandAfterXTestBackspaceWaitMs": 3,
            "DbusAfterXTestBackspaceWaitMs": 5,
            "IBusAfterXTestBackspaceWaitMs": 5,
        }
        for key, value in preset_values.items():
            self.store.advanced.set("", key, str(value))
        self._applying_preset = True
        try:
            for key, value in preset_values.items():
                spin = self.advanced_spins.get(key)
                if spin is not None:
                    spin.set_value(value)
        finally:
            self._applying_preset = False
        self._save_and_report("Đã áp dụng preset hiệu năng", self.store.advanced)

    def _build_appearance(self) -> Gtk.Widget:
        page, content = self._page("Giao diện", "Chọn bảng màu cho cửa sổ cài đặt. Thay đổi được áp dụng và lưu ngay.")
        group = self._section(content, "Chủ đề màu sắc")
        group.add_css_class("list-group")
        grid = Gtk.FlowBox()
        grid.set_selection_mode(Gtk.SelectionMode.NONE)
        grid.set_min_children_per_line(2)
        grid.set_max_children_per_line(2)
        grid.set_homogeneous(True)
        grid.add_css_class("theme-grid")
        self.theme_buttons = {}
        for key, (name, _) in THEMES.items():
            button = Gtk.ToggleButton()
            button.add_css_class("theme-choice")
            box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
            label = Gtk.Label(label=name, xalign=0)
            label.add_css_class("setting-title")
            box.append(label)
            swatches = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=6)
            for index in range(5):
                swatch = Gtk.Box(hexpand=True)
                swatch.add_css_class("theme-swatch")
                swatch.add_css_class(f"swatch-{key}-{index}")
                swatches.append(swatch)
            if key == "System":
                detail = Gtk.Label(label="Tự động theo màu giao diện desktop", xalign=0, wrap=True)
                detail.add_css_class("setting-description")
                box.append(detail)
            else:
                box.append(swatches)
            button.set_child(box)
            button.set_active(key == self.theme)
            button.connect("clicked", self._select_theme, key)
            self.theme_buttons[key] = button
            grid.insert(button, -1)
        group.append(grid)

        radius_group = self._section(content, "Bo góc cửa sổ")
        radius_group.add_css_class("list-group")
        radius_row = self._row(radius_group, "Độ bo góc", "Chọn độ bo cho khung cửa sổ Settings.")
        radius_choices = ["Không bo", "5 px", "15 px", "24 px"]
        radius_values = ["0", "5", "15", "24"]
        radius_dropdown = Gtk.DropDown.new_from_strings(radius_choices)
        radius_dropdown.set_selected(enum_index(self.window_radius, radius_values, 3))
        radius_dropdown.set_size_request(205, -1)
        radius_dropdown.set_valign(Gtk.Align.CENTER)
        radius_dropdown.connect("notify::selected", self._select_window_radius, radius_values)
        self.radius_dropdown = radius_dropdown
        radius_row.append(radius_dropdown)
        return page

    def _select_theme(self, _button: Gtk.Button | None, key: str) -> None:
        if key != self.theme:
            # Read again to preserve any unrelated settings edited externally.
            document = ConfigDocument(self.appearance.path)
            document.set("Appearance", "Theme", key)
            try:
                document.save()
            except OSError as error:
                self._saved(f"Lỗi lưu giao diện: {error}")
            else:
                self.appearance = document
                self.theme = key
                self.css_provider.load_from_data(self._css())
                self._saved(f"Đã áp dụng: {THEMES[key][0]}")
        for name, button in self.theme_buttons.items():
            button.set_active(name == self.theme)

    def _select_window_radius(
        self, control: Gtk.DropDown, _param, values: list[str]
    ) -> None:
        index = control.get_selected()
        if index >= len(values):
            return
        self._select_window_radius_value(values[index])

    def _select_window_radius_value(self, radius: str) -> None:
        if radius == self.window_radius:
            return
        document = ConfigDocument(self.appearance.path)
        document.set("Appearance", "WindowRadius", radius)
        try:
            document.save()
        except OSError as error:
            self._saved(f"Lỗi lưu độ bo góc: {error}")
            return
        self.appearance = document
        self.window_radius = radius
        if self.radius_dropdown is not None:
            self.radius_dropdown.set_selected({"0": 0, "5": 1, "15": 2, "24": 3}[radius])
        self.css_provider.load_from_data(self._css())
        label = "Không bo" if radius == "0" else f"{radius} px"
        self._saved(f"Đã áp dụng độ bo góc {label}")

    def _selected_icon_path(self, target: str) -> Path:
        path = self.appearance.get("Icons", f"{target}Path")
        return Path(path) if path and Path(path).is_file() else ORIGINAL_ICON

    def _build_icons(self) -> Gtk.Widget:
        page, content = self._page("Biểu tượng", "Một chút vui vẻ cho Areca. Chọn một biểu tượng để dùng cho cả Settings và khay hệ thống.")
        current = self._section(content, "Biểu tượng đang dùng")
        row = self._row(
            current, "Settings và khay hệ thống",
            "Biểu tượng dùng chung cho cửa sổ, trình khởi chạy GTK và khay Fcitx5.",
        )
        self.icon_label = Gtk.Label(xalign=1)
        self.icon_label.add_css_class("setting-description")
        row.append(self.icon_label)
        self.icon_preview = Gtk.Image.new_from_file(str(self._selected_icon_path("Settings")))
        self.icon_preview.set_pixel_size(48)
        self.icon_preview.add_css_class("icon-preview")
        row.append(self.icon_preview)
        choices = self._section(content, "Chọn biểu tượng")
        choices.set_tooltip_text("Lựa chọn sẽ cập nhật đồng thời biểu tượng Settings và khay hệ thống")
        grid = Gtk.FlowBox()
        grid.set_selection_mode(Gtk.SelectionMode.NONE)
        grid.set_min_children_per_line(3)
        grid.set_max_children_per_line(3)
        grid.set_homogeneous(True)
        grid.add_css_class("icon-grid")
        self.icon_buttons = {}
        for key, (name, path) in PRESETS.items():
            button = Gtk.ToggleButton()
            button.add_css_class("icon-choice")
            box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
            preview = Gtk.Image.new_from_file(str(path))
            preview.set_pixel_size(56)
            box.append(preview)
            label = Gtk.Label(label=name, wrap=True)
            box.append(label)
            button.set_child(box)
            button.set_tooltip_text(f"Dùng biểu tượng {name}")
            button.connect("clicked", self._choose_icon_preset, key)
            self.icon_buttons[key] = button
            grid.insert(button, -1)
        choices.append(grid)
        upload = self._about_action("Chọn ảnh từ máy…", "image")
        upload.set_halign(Gtk.Align.START)
        upload.set_margin_top(12)
        upload.add_css_class("suggested-action")
        upload.connect("clicked", self._choose_icon_file)
        content.append(upload)
        hint = Gtk.Label(label="PNG, JPEG, SVG hoặc WebP · Tối đa 16 MB. Ảnh được giữ đúng tỉ lệ và sao chép vào thư viện của bạn.", xalign=0, wrap=True)
        hint.add_css_class("setting-description")
        hint.set_margin_top(8)
        content.append(hint)
        return page

    def _refresh_icon_previews(self) -> None:
        if self.icon_preview and self.icon_label:
            self.icon_preview.set_from_file(str(self._selected_icon_path("Settings")))
            settings_key = self.appearance.get("Icons", "SettingsPreset", "original")
            tray_key = self.appearance.get("Icons", "TrayPreset", "original")
            if settings_key != tray_key:
                self.icon_label.set_text("Chưa đồng bộ")
            else:
                self.icon_label.set_text(PRESETS[settings_key][0] if settings_key in PRESETS else "Ảnh riêng")
        key = self.appearance.get("Icons", "SettingsPreset", "original")
        for name, button in self.icon_buttons.items():
            button.set_active(name == key)
        path = str(self._selected_icon_path("Settings"))
        self.brand_logo.set_from_file(path)
        self.about_logo.set_from_file(path)
        name = self.appearance.get("Icons", "SettingsName")
        if name:
            self.set_icon_name(name)

    def _choose_icon_preset(self, _button: Gtk.Button, key: str) -> None:
        self._apply_both_icons(key, PRESETS[key][1])

    def _choose_icon_file(self, _button: Gtk.Button) -> None:
        dialog = Gtk.FileDialog()
        dialog.set_title("Chọn biểu tượng cho Settings và khay hệ thống")
        file_filter = Gtk.FileFilter()
        file_filter.set_name("Ảnh (PNG, JPEG, SVG, WebP)")
        for mime in ("image/png", "image/jpeg", "image/svg+xml", "image/webp"):
            file_filter.add_mime_type(mime)
        filters = Gio.ListStore.new(Gtk.FileFilter)
        filters.append(file_filter)
        dialog.set_filters(filters)
        dialog.open(self, None, self._icon_file_chosen, None)

    def _icon_file_chosen(
        self, dialog: Gtk.FileDialog, result: Gio.AsyncResult, _user_data=None
    ) -> None:
        try:
            file = dialog.open_finish(result)
        except GLib.Error as error:
            if not error.matches(Gtk.dialog_error_quark(), Gtk.DialogError.DISMISSED):
                self._saved(f"Lỗi chọn ảnh: {error.message}")
            return
        path = file.get_path()
        if path:
            self._apply_both_icons("custom", Path(path))
        else:
            self._saved("Không mở được ảnh: hãy chọn tệp trên máy")

    def _apply_both_icons(self, preset: str, source: Path) -> bool:
        old_settings = (
            self.appearance.get("Icons", "SettingsPreset", "original"),
            self._selected_icon_path("Settings"),
        )
        if not self._apply_icon("Settings", preset, source):
            return False
        if not self._apply_icon("Tray", preset, source):
            old_preset, old_source = old_settings
            if not self._apply_icon("Settings", old_preset, old_source):
                self._saved("Không đổi được cả hai icon; khôi phục icon Settings cũ thất bại")
            else:
                self._saved("Không đổi được cả hai icon; đã giữ lại lựa chọn cũ")
            return False
        self._saved("Đã đổi biểu tượng Settings và khay hệ thống")
        return True

    def _apply_icon(self, target: str, preset: str, source: Path) -> bool:
        document = ConfigDocument(self.appearance.path)
        rollback: list[tuple[Path, bytes | None]] = []
        try:
            name, path = prepare_icon(source, target)
            if target == "Tray" and preset == "original":
                name = DEFAULT_TRAY_ICON
            document.set("Icons", f"{target}Preset", preset)
            document.set("Icons", f"{target}Name", name)
            document.set("Icons", f"{target}Path", str(path))
            if target == "Settings":
                launcher, payload = desktop_entry(name)
                rollback.append((launcher, launcher.read_bytes() if launcher.exists() else None))
                atomic_write(launcher, payload)
            else:
                main = ConfigDocument(self.store.main.path)
                rollback.append((main.path, main.path.read_bytes() if main.path.exists() else None))
                main.set("", "TrayIcon", name)
                main.save()
            document.save()
        except (OSError, GLib.Error, ValueError) as error:
            for changed_path, previous in reversed(rollback):
                try:
                    if previous is None:
                        changed_path.unlink(missing_ok=True)
                    else:
                        atomic_write(changed_path, previous)
                except OSError:
                    self._saved("Lỗi lưu biểu tượng và khôi phục cấu hình; hãy kiểm tra quyền ghi")
                    return False
            self._saved(f"Lỗi lưu biểu tượng: {error}")
            self._refresh_icon_previews()
            return False
        self.appearance = document
        fcitx_reloaded = True
        if target == "Tray":
            self.store.main.lines = main.lines
            fcitx_reloaded, fcitx_error = self.store.reload_fcitx()
        else:
            fcitx_error = ""
        icon_theme = Gtk.IconTheme.get_for_display(self.get_display())
        icon_theme.set_search_path(icon_theme.get_search_path())
        self._refresh_icon_previews()
        message = "Đã đổi biểu tượng " + ("Settings" if target == "Settings" else "khay hệ thống")
        if target == "Tray":
            message += "; " + (
                "Fcitx5 đã nạp lại addon Areca" if fcitx_reloaded
                else f"đã lưu, nhưng Areca chưa áp dụng: {fcitx_error}"
            )
        self._saved(message)
        return True

    def _build_about(self) -> Gtk.Widget:
        page, content = self._page("", "")
        identity = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        identity.set_halign(Gtk.Align.CENTER)
        identity.set_margin_top(44)
        identity.set_margin_bottom(26)
        logo_path = self._selected_icon_path("Settings")
        logo = Gtk.Image.new_from_file(str(logo_path))
        logo.set_pixel_size(88)
        logo.set_size_request(88, 88)
        self.about_logo = logo
        identity.append(logo)
        title = Gtk.Label(label="Areca", xalign=0.5)
        title.add_css_class("page-title")
        version = Gtk.Label(label=f"Phiên bản {APP_VERSION}", xalign=0.5)
        version.add_css_class("dim-label")
        identity.append(title)
        identity.append(version)
        content.append(identity)

        introduction = Gtk.Label(
            label="Areca là bộ gõ tiếng Việt cho Linux, chạy trên Fcitx5 và tối ưu cho Wayland.",
            wrap=True,
            justify=Gtk.Justification.CENTER,
        )
        introduction.set_halign(Gtk.Align.CENTER)
        introduction.set_max_width_chars(72)
        introduction.add_css_class("page-description")
        content.append(introduction)

        email = Gtk.LinkButton.new_with_label(
            "mailto:kimxuanhong@outlook.com", "Liên hệ: kimxuanhong@outlook.com"
        )
        email.set_halign(Gtk.Align.CENTER)
        email.add_css_class("flat")
        content.append(email)

        actions = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=12)
        actions.set_halign(Gtk.Align.CENTER)
        report_button = self._about_action("Báo lỗi", "bug")
        report_button.connect(
            "clicked", self._open_about_link,
            "https://github.com/xhkzeroone/ArecaIME/issues/new", "Đang mở GitHub Issues…",
        )
        restart_button = self._about_action("Nạp lại Fcitx5", "refresh")
        restart_button.set_tooltip_text("Yêu cầu Fcitx5 nạp lại cấu hình và addon")
        restart_button.connect("clicked", self._restart_fcitx_from_about)
        update_button = self._about_action("Kiểm tra cập nhật", "refresh")
        update_button.add_css_class("suggested-action")
        update_button.connect(
            "clicked", self._open_about_link,
            "https://github.com/xhkzeroone/ArecaIME/releases/latest", "Đang mở bản phát hành mới nhất…",
        )
        actions.append(report_button)
        actions.append(restart_button)
        actions.append(update_button)
        content.append(actions)
        return page

    @staticmethod
    def _about_action(label: str, icon_name: str) -> Gtk.Button:
        button = Gtk.Button()
        contents = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        contents.append(ui_icon(icon_name))
        contents.append(Gtk.Label(label=label))
        button.set_child(contents)
        return button

    def _open_about_link(self, _button: Gtk.Button, uri: str, message: str) -> None:
        try:
            opened = Gio.AppInfo.launch_default_for_uri(uri, None)
        except GLib.Error as error:
            self._saved(f"Không mở được liên kết: {error.message}")
            return
        self._saved(message if opened else "Không tìm thấy ứng dụng để mở liên kết")

    def _restart_fcitx_from_about(self, _button: Gtk.Button) -> None:
        restarted, error = self.store.restart_fcitx()
        if restarted:
            self._saved("Đã yêu cầu Fcitx5 nạp lại")
        else:
            self._saved(f"Không nạp lại được Fcitx5: {error}")

    def _on_switch(self, control: Gtk.Switch, _param, document: ConfigDocument, key: str) -> None:
        document.set("", key, "True" if control.get_active() else "False")
        self._save_and_report("Đã lưu cấu hình", document)

    def _on_dropdown(
        self, control: Gtk.DropDown, _param, document: ConfigDocument, section: str, key: str, choices: list[str]
    ) -> None:
        index = control.get_selected()
        if index < len(choices):
            document.set(section, key, choices[index])
            self._save_and_report("Đã lưu cấu hình", document)

    def _on_spin(self, control: Gtk.SpinButton, key: str) -> None:
        if self._applying_preset:
            return
        self.store.advanced.set("", key, str(int(control.get_value())))
        self._save_and_report("Đã lưu cấu hình", self.store.advanced)

    def _save_and_report(self, message: str, *documents: ConfigDocument) -> bool:
        try:
            reloaded, error = self.store.save(*documents)
        except OSError as error:
            self._saved(f"Lỗi lưu cấu hình: {error}")
            return False
        if reloaded:
            self._saved(f"{message}; Fcitx5 đã nạp lại cấu hình Areca")
        else:
            self._saved(f"{message}, nhưng Areca chưa áp dụng: {error}")
        return reloaded

    def _saved(self, message: str) -> None:
        self.status_label.set_text(message)
        self.toast_label.set_text(message)
        is_error = (
            message.startswith(("Chưa lưu", "Không", "Lỗi"))
            or "chưa áp dụng:" in message
            or "chưa khởi động lại:" in message
        )
        if is_error:
            self.toast_box.add_css_class("error")
        else:
            self.toast_box.remove_css_class("error")
        self.toast_revealer.set_reveal_child(True)
        if self._toast_timeout_id:
            GLib.source_remove(self._toast_timeout_id)
        self._toast_timeout_id = GLib.timeout_add_seconds(3, self._hide_toast)

    def _hide_toast(self) -> bool:
        self.toast_revealer.set_reveal_child(False)
        self._toast_timeout_id = 0
        return GLib.SOURCE_REMOVE

    def _restore_defaults(self, _button: Gtk.Button) -> None:
        page = self.active_page
        if page in {"overview", "about"}:
            return
        page_names = {
            "typing": "Bộ gõ",
            "macros": "Macro",
            "apps": "Ứng dụng",
            "advanced": "Nâng cao",
            "appearance": "Giao diện",
            "icons": "Biểu tượng",
        }
        dialog = Gtk.AlertDialog()
        dialog.set_message(f"Khôi phục mặc định cho {page_names[page]}?")
        dialog.set_detail("Các thay đổi hiện tại của mục này sẽ bị thay thế.")
        dialog.set_buttons(["Hủy", "Khôi phục"])
        dialog.set_cancel_button(0)
        dialog.set_default_button(0)
        dialog.choose(self, None, self._confirm_restore_defaults, page)

    def _confirm_restore_defaults(
        self, dialog: Gtk.AlertDialog, result: Gio.AsyncResult, page: str
    ) -> None:
        try:
            response = dialog.choose_finish(result)
        except GLib.Error:
            return
        if response != 1:
            return

        if page == "icons":
            self._apply_both_icons("original", ORIGINAL_ICON)
            return

        if page == "appearance":
            self._select_theme(None, "Light")
            self._select_window_radius_value("24")
            return

        documents = []
        if page == "typing":
            self._set_main_defaults()
            documents.append(self.store.main)
        elif page == "macros":
            self.store.macros.replace_indexed("Macro", [])
            for key in ("EnableMacro", "CapitalizeMacro", "RevertMacroOnBackspace", "MacroBypassModifier"):
                self.store.main.set("", key, str(MAIN_DEFAULTS[key]))
            documents.extend((self.store.macros, self.store.main))
        elif page == "apps":
            self.store.apps.replace_indexed("AppOverrides", [])
            self.store.main.replace_list_values("SelectBackendKey", MAIN_KEY_LIST_DEFAULTS["SelectBackendKey"])
            documents.extend((self.store.apps, self.store.main))
        elif page == "advanced":
            for key, value in ADVANCED_DEFAULTS.items():
                self.store.advanced.set("", key, str(value))
            self.store.main.set("", "BackspaceRecovery", str(MAIN_DEFAULTS["BackspaceRecovery"]))
            documents.extend((self.store.advanced, self.store.main))

        self._save_and_report("Đã khôi phục mặc định", *documents)
        self._rebuild_page(page)
        self.show_page(page)

    def _set_main_defaults(self) -> None:
        for key, value in MAIN_DEFAULTS.items():
            self.store.main.set("", key, str(value))
        self.store.main.replace_list_values("SwitchModeKey", MAIN_KEY_LIST_DEFAULTS["SwitchModeKey"])
        self.store.main.replace_list_values("SelectBackendKey", MAIN_KEY_LIST_DEFAULTS["SelectBackendKey"])

    def _rebuild_page(self, page: str) -> None:
        builders = {
            "overview": self._build_overview,
            "typing": self._build_typing,
            "macros": self._build_macros,
            "apps": self._build_apps,
            "advanced": self._build_advanced,
            "appearance": self._build_appearance,
            "icons": self._build_icons,
        }
        old_page = self.pages[page]
        self.stack.remove(old_page)
        new_page = builders[page]()
        self.pages[page] = new_page
        self.stack.add_named(new_page, page)

    def _on_nav_toggled(self, button: Gtk.ToggleButton, page_id: str) -> None:
        if button.get_active():
            self.show_page(page_id)

    def show_page(self, page_id: str) -> None:
        if page_id == "overview":
            self._rebuild_page("overview")
        self.active_page = page_id
        self.stack.set_visible_child_name(page_id)
        for name, button in self.navigation.items():
            if name != page_id:
                button.set_active(False)
        self.navigation[page_id].set_active(True)
        self.restore_button.set_visible(page_id not in {"overview", "about"})


class ArecaSettings(Gtk.Application):
    def __init__(self):
        super().__init__(application_id=APP_ID, flags=Gio.ApplicationFlags.DEFAULT_FLAGS)

    def do_activate(self) -> None:
        window = self.props.active_window
        if window is None:
            window = SettingsWindow(self)
        window.present()


def self_test() -> None:
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "test.conf"
        path.write_text("# preserved\nFoo=bar\n\n[AppOverrides/0]\nAppName=old\n", encoding="utf-8")
        document = ConfigDocument(path)
        document.set("", "Foo", "new value")
        document.replace_indexed("AppOverrides", [{"AppName": "new-app", "Mode": "Shift select"}])
        document.save()
        restored = ConfigDocument(path)
        assert restored.get("", "Foo") == "new value"
        assert restored.indexed("AppOverrides") == [{"AppName": "new-app", "Mode": "Shift select"}]
        assert "# preserved" in path.read_text(encoding="utf-8")
        restored.replace_list_values("SelectBackendKey", ["grave", "Ctrl+space"])
        assert restored.list_values("SelectBackendKey") == ["grave", "Ctrl+space"]
    print("GTK settings config tests passed")


if __name__ == "__main__":
    if "--self-test" in sys.argv:
        self_test()
    else:
        raise SystemExit(ArecaSettings().run(sys.argv))
