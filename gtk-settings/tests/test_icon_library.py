"""Icon import/export checks; image decoding needs the desktop image-loader service."""
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from icon_library import PRESETS, APP_ID, desktop_entry, prepare_icon
from gi.repository import GdkPixbuf, Gio, GLib


class IconLibraryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        env = patch.dict(os.environ, {'XDG_DATA_HOME': str(self.root / 'data')})
        env.start()
        self.addCleanup(env.stop)

    def test_presets_export_and_resolve_to_self_contained_png(self):
        names = set()
        for key, (_, source) in PRESETS.items():
            with self.subTest(preset=key):
                name, path = prepare_icon(source, 'Settings')
                names.add(name)
                for size in (32, 64, 128, 256):
                    icon = path.parents[2] / f'{size}x{size}/apps/{name}.png'
                    image = GdkPixbuf.Pixbuf.new_from_file(str(icon))
                    self.assertEqual((image.get_width(), image.get_height()), (size, size))
                self.assertEqual(prepare_icon(source, 'Settings')[0], name)
        self.assertEqual(len(names), len(PRESETS))

    def test_non_square_photo_keeps_aspect_ratio_and_survives_source_removal(self):
        source = self.root / 'wide.png'
        image = GdkPixbuf.Pixbuf.new(GdkPixbuf.Colorspace.RGB, True, 8, 200, 100)
        image.fill(0xff0000ff)
        image.savev(str(source), 'png', [], [])
        _, path = prepare_icon(source, 'Tray')
        source.unlink()
        output = GdkPixbuf.Pixbuf.new_from_file(str(path))
        pixels = output.get_pixels()
        stride = output.get_rowstride()
        self.assertEqual(pixels[128 * 4 + 3], 0)
        self.assertEqual(pixels[128 * stride + 128 * 4 + 3], 255)

    def test_rejects_corrupt_or_oversize_files_before_export(self):
        source = self.root / 'invalid.png'
        source.write_bytes(b'not an image')
        with self.assertRaises(GLib.Error):
            prepare_icon(source, 'Tray')
        with source.open('wb') as handle:
            handle.truncate(16 * 1024 * 1024 + 1)
        with self.assertRaises(ValueError):
            prepare_icon(source, 'Tray')
        self.assertFalse((self.root / 'data').exists())

    def test_launcher_has_matching_application_id_and_icon(self):
        path, payload = desktop_entry('areca-settings-test')
        path.parent.mkdir(parents=True)
        path.write_bytes(payload)
        app = Gio.DesktopAppInfo.new_from_filename(str(path))
        self.assertIsNotNone(app)
        self.assertEqual(path.stem, APP_ID)
        self.assertEqual(app.get_string('Icon'), 'areca-settings-test')
        self.assertIn('areca_settings.py', app.get_commandline())


if __name__ == '__main__':
    unittest.main()
