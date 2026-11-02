"""Areca-owned SVG UI icons and user-selected desktop/tray artwork."""
from __future__ import annotations

import hashlib
import os
from pathlib import Path
import sys
import tempfile

import gi

gi.require_version("Gtk", "4.0")
gi.require_version("GdkPixbuf", "2.0")
from gi.repository import GdkPixbuf, Gio, Gtk

ASSETS = Path(__file__).resolve().parent / "assets"
ORIGINAL_ICON = Path(__file__).resolve().parent.parent / "icons/hicolor/scalable/apps/areca-settings.svg"
APP_ID = "io.github.areca.Settings"
DEFAULT_TRAY_ICON = "org.fcitx.Fcitx5.fcitx-areca"
PRESETS = {
    "original": ("Areca gốc", ORIGINAL_ICON),
    "sprout": ("Mầm xanh", ASSETS / "mascots/sprout.svg"),
    "cat": ("Mèo mơ màng", ASSETS / "mascots/cat.svg"),
    "cloud": ("Mây rong chơi", ASSETS / "mascots/cloud.svg"),
    "flower": ("Hoa má hồng", ASSETS / "mascots/flower.svg"),
    "orange": ("Cam cười", ASSETS / "mascots/orange.svg"),
    "star": ("Sao lí lắc", ASSETS / "mascots/star.svg"),
}


def ui_icon(name: str, size: int = 20) -> Gtk.Image:
    image = Gtk.Image.new_from_gicon(Gio.FileIcon.new(Gio.File.new_for_path(str(ASSETS / "ui" / f"{name}-symbolic.svg"))))
    image.set_pixel_size(size)
    return image


def icon_button(name: str, tooltip: str) -> Gtk.Button:
    button = Gtk.Button()
    button.set_child(ui_icon(name))
    button.set_tooltip_text(tooltip)
    return button


def data_home() -> Path:
    return Path(os.environ.get("XDG_DATA_HOME") or Path.home() / ".local/share")


def atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def prepare_icon(source: Path, target: str) -> tuple[str, Path]:
    """Decode once and keep a self-contained, square PNG; never link the source."""
    if target not in {"Settings", "Tray"}:
        raise ValueError("Loại biểu tượng không hợp lệ")
    if source.stat().st_size > 16 * 1024 * 1024:
        raise ValueError("Chọn ảnh nhỏ hơn 16 MB")
    picture = GdkPixbuf.Pixbuf.new_from_file_at_scale(str(source), 256, 256, True)
    canvas = GdkPixbuf.Pixbuf.new(GdkPixbuf.Colorspace.RGB, True, 8, 256, 256)
    canvas.fill(0)
    picture.copy_area(0, 0, picture.get_width(), picture.get_height(), canvas,
                      (256 - picture.get_width()) // 2, (256 - picture.get_height()) // 2)
    success, payload = canvas.save_to_bufferv("png", [], [])
    if not success:
        raise ValueError("Không chuyển được ảnh thành biểu tượng")
    name = f"areca-{target.lower()}-{hashlib.sha256(payload).hexdigest()[:20]}"
    # Also provide the standard unthemed fallback for desktops whose hicolor
    # index does not list every size directory in the user's icon tree.
    atomic_write(data_home() / f"icons/{name}.png", payload)
    for size in (32, 64, 128, 256):
        scaled = canvas if size == 256 else canvas.scale_simple(size, size, GdkPixbuf.InterpType.BILINEAR)
        success, data = scaled.save_to_bufferv("png", [], [])
        if not success:
            raise ValueError("Không tạo được kích thước biểu tượng")
        atomic_write(data_home() / f"icons/hicolor/{size}x{size}/apps/{name}.png", data)
    return name, data_home() / f"icons/hicolor/256x256/apps/{name}.png"


def desktop_entry(icon_name: str) -> tuple[Path, bytes]:
    # Desktop Exec syntax, not a shell command. Escape percent field codes too.
    def argument(value: str) -> str:
        escaped = ''.join('\\' + c if c in '\\"`$' else c for c in value)
        return '"' + escaped.replace('\\', '\\\\').replace('%', '%%') + '"'
    script = Path(__file__).resolve().parent / "areca_settings.py"
    text = (
        "[Desktop Entry]\nType=Application\nName=Areca Settings\n"
        "Name[vi]=Cài đặt Areca\n"
        f"Exec={argument(sys.executable)} {argument(str(script))}\n"
        f"Icon={icon_name}\nStartupWMClass={APP_ID}\n"
        "Terminal=false\nCategories=Settings;\nStartupNotify=true\n"
    )
    return data_home() / f"applications/{APP_ID}.desktop", text.encode()
