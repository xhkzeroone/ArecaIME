"""Run with: python3 -m unittest discover -s gtk-settings/tests"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from areca_settings import THEMES, contrast_ratio, mix_color, theme_text_colors


class ThemeContrastTests(unittest.TestCase):
    def test_reference_contrast(self):
        self.assertAlmostEqual(contrast_ratio('#000000', '#ffffff'), 21)
        self.assertAlmostEqual(contrast_ratio('#777777', '#777777'), 1)

    def test_preset_text_on_all_surfaces_and_accent_states(self):
        for name, (_, colors) in THEMES.items():
            if colors is None:
                continue
            with self.subTest(theme=name):
                bg, sidebar, surface, fg, accent = colors
                muted, accent_text, on_accent = theme_text_colors(colors)
                for base in (bg, sidebar, surface):
                    self.assertGreaterEqual(contrast_ratio(fg, base), 4.5)
                    self.assertGreaterEqual(contrast_ratio(muted, base), 4.5)
                    for opacity in (0, .08, .16, .22, .24, .32):
                        self.assertGreaterEqual(
                            contrast_ratio(accent_text, mix_color(accent, base, opacity)), 4.5
                        )
                self.assertGreaterEqual(contrast_ratio(on_accent, accent), 4.5)


if __name__ == '__main__':
    unittest.main()
