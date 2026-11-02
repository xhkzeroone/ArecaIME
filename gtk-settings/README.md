# Areca GTK Settings

The GTK4/PyGObject settings application is installed as the default Areca Settings app in release packages and replaces the legacy ImGui launcher. The C++/ImGui app remains available as an optional developer build with `-DARECA_BUILD_LEGACY_SETTINGS=ON`.

## Run

```sh
python3 gtk-settings/areca_settings.py
```

Dependencies: Python 3, PyGObject, and GTK 4 (`python3-gi` and `gir1.2-gtk-4.0` on Debian/Ubuntu).

The app edits Areca's existing files under `~/.config/fcitx5/conf/`: `areca.conf`, `areca-macro-table.conf`, `areca-apps.conf`, and `areca-advanced.conf`. Updates are written atomically, then sent to Fcitx5 through the same `ReloadAddonConfig("areca")` session D-Bus call used by the original settings app. The UI reports when the file was saved but the running Areca addon could not reload it.

The **Giao diện** tab defaults to Light and offers the nine presets from the original settings app: Light, Dark, Areca Dark, Gruvbox, Catppuccin Mocha, Catppuccin Latte, Dracula, One Dark, and Tokyo Night, plus a system color scheme option. Colors apply immediately, including to confirmation dialogs. The selected theme is saved atomically in `[Appearance] Theme` in `areca-gtk-settings.conf` in the same config directory (`XDG_CONFIG_HOME` is respected). Appearance changes do not reload Fcitx5 or modify the original settings app's appearance file. Restoring defaults on this tab returns to Light.

The **Biểu tượng** tab uses a set of Areca-drawn navigation and action icons and includes six playful mascot choices plus the original Areca mark. Choose one mascot or import a PNG, JPEG, SVG, or WebP image up to 16 MB; the same selection updates both Settings and the Fcitx5 tray. Imported art is copied into the user's hicolor icon theme as transparent, square PNG sizes, so the source file can be moved later. A per-user GTK launcher is created for the customized Settings icon. Tray presets and imported icons are read by the Areca Fcitx5 engine from `TrayIcon` in `areca.conf`; applying settings calls `ReloadAddonConfig("areca")`, then `fcitx5-remote -r`. Restore defaults to return both icons to the original mark.

Run the config document round-trip check with:

```sh
python3 gtk-settings/areca_settings.py --self-test
```

Check preset text contrast (normal text, secondary text, accent button states, and selection text):

```sh
python3 -m unittest discover -s gtk-settings/tests
```
