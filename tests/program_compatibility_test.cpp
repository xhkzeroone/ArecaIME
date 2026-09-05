#include "program_compatibility.h"
#include "types.h"

#include <cassert>

int main() {
  using areca::isVSCodeFamilyProgram;

  // Existing VS Code-family matching remains intact.
  assert(isVSCodeFamilyProgram("code"));
  assert(isVSCodeFamilyProgram("/usr/bin/code-insiders"));
  assert(isVSCodeFamilyProgram("VSCodium.desktop"));

  // Distribution/desktop terminals can be reported as executables or app IDs.
  assert(isVSCodeFamilyProgram("gnome-terminal-server"));
  assert(isVSCodeFamilyProgram("org.gnome.Console.desktop"));
  assert(isVSCodeFamilyProgram("xfce4-terminal"));
  assert(isVSCodeFamilyProgram("com.system76.CosmicTerm.desktop"));

  // Common third-party terminal emulators and development app IDs.
  assert(isVSCodeFamilyProgram("/usr/bin/alacritty"));
  assert(isVSCodeFamilyProgram("net.kovidgoyal.kitty.desktop"));
  assert(isVSCodeFamilyProgram("com.mitchellh.ghostty"));
  assert(isVSCodeFamilyProgram("app.devsuite.Ptyxis.Devel.desktop"));
  assert(isVSCodeFamilyProgram("dev.warp.Warp-Stable.desktop"));
  assert(isVSCodeFamilyProgram("dev.warp.Warp.desktop"));

  // General-purpose IDEs, code editors and their package-channel app IDs.
  assert(isVSCodeFamilyProgram("jetbrains-idea-ultimate.desktop"));
  assert(isVSCodeFamilyProgram("com.jetbrains.PyCharm-Professional.desktop"));
  assert(isVSCodeFamilyProgram("com.google.AndroidStudio.desktop"));
  assert(isVSCodeFamilyProgram("org.eclipse.Eclipse"));
  assert(isVSCodeFamilyProgram("dev.zed.Zed.desktop"));
  assert(isVSCodeFamilyProgram("org.gnome.Builder.Devel.desktop"));
  assert(isVSCodeFamilyProgram("/usr/bin/neovide"));

  // Specialized IDEs and other development tools.
  assert(isVSCodeFamilyProgram("org.spyder-ide.spyder"));
  assert(isVSCodeFamilyProgram("cc.arduino.IDE2.desktop"));
  assert(isVSCodeFamilyProgram("org.godotengine.Godot"));
  assert(isVSCodeFamilyProgram("io.dbeaver.DBeaverCommunity.desktop"));
  assert(isVSCodeFamilyProgram("com.getpostman.Postman"));
  assert(isVSCodeFamilyProgram("com.axosoft.GitKraken.desktop"));

  // Exact matching prevents unrelated applications from inheriting the rule.
  assert(!isVSCodeFamilyProgram("terminal-notes"));
  assert(!isVSCodeFamilyProgram("wavebox"));
  assert(!isVSCodeFamilyProgram("footage"));
  assert(!isVSCodeFamilyProgram("stationeers"));
  assert(!isVSCodeFamilyProgram("idea-board"));
  assert(!isVSCodeFamilyProgram("studio-one"));
  assert(!isVSCodeFamilyProgram("atomizer"));
  assert(!isVSCodeFamilyProgram("fleet-manager"));
  assert(!isVSCodeFamilyProgram("firefox"));
  assert(!isVSCodeFamilyProgram(""));

  using areca::isTerminalProgram;
  assert(isTerminalProgram("ghostty"));
  assert(isTerminalProgram("com.mitchellh.ghostty"));
  assert(isTerminalProgram("org.gnome.Console.desktop"));
  assert(isTerminalProgram("app.devsuite.Ptyxis.desktop"));
  assert(isTerminalProgram("com.raggesilver.BlackBox.desktop"));
  assert(isTerminalProgram("gnome-terminal-server"));
  assert(isTerminalProgram("mate-terminal"));
  assert(isTerminalProgram("xfce4-terminal"));
  assert(isTerminalProgram("io.elementary.terminal"));
  // KDE terminals deliberately stay outside the terminal compatibility path.
  assert(!isTerminalProgram("konsole"));
  assert(!isTerminalProgram("org.kde.Konsole.desktop"));
  assert(!isTerminalProgram("org.kde.Konsole.Devel.desktop"));
  assert(!isTerminalProgram("yakuake"));
  assert(!isTerminalProgram("org.kde.Yakuake.desktop"));
  assert(!isVSCodeFamilyProgram("konsole"));
  assert(!isVSCodeFamilyProgram("org.kde.Konsole.Devel.desktop"));
  assert(!isVSCodeFamilyProgram("org.kde.Yakuake.desktop"));
  assert(!isVSCodeFamilyProgram("kate"));
  assert(!isVSCodeFamilyProgram("org.kde.Kate.desktop"));
  assert(!isVSCodeFamilyProgram("org.kde.Kate.Devel.desktop"));
  assert(!isVSCodeFamilyProgram("kdevelop"));
  assert(!isVSCodeFamilyProgram("org.kde.KDevelop.desktop"));
  assert(!isVSCodeFamilyProgram("org.kde.KDevelop.Devel.desktop"));
  assert(!isVSCodeFamilyProgram("kdiff3"));
  assert(!isVSCodeFamilyProgram("org.kde.KDiff3.desktop"));
  assert(!isVSCodeFamilyProgram("org.kde.Dolphin.desktop"));
  assert(!isVSCodeFamilyProgram("org.kde.Okular.desktop"));
  assert(!isVSCodeFamilyProgram("org.kde.Gwenview.desktop"));
  assert(!isVSCodeFamilyProgram("org.kde.Ark.desktop"));
  assert(!isVSCodeFamilyProgram("org.kde.Spectacle.desktop"));
  assert(!isTerminalProgram("firefox"));
  assert(!isTerminalProgram(""));

  using areca::resolveAfterBackspaceWaitMs;
  using areca::resolveAfterUinputShiftSelectWaitMs;
  areca::RewritePlan plan;
  plan.afterBackspaceWaitMs = 10;
  plan.waylandAfterBackspaceWaitMs = 3;
  plan.ximAfterBackspaceWaitMs = 10;
  plan.fcitx4AfterBackspaceWaitMs = 10;
  plan.dbusAfterBackspaceWaitMs = 5;

  plan.afterUinputShiftSelectWaitMs = 20;
  plan.waylandAfterUinputShiftSelectWaitMs = 8;
  plan.ximAfterUinputShiftSelectWaitMs = 20;
  plan.fcitx4AfterUinputShiftSelectWaitMs = 20;
  plan.dbusAfterUinputShiftSelectWaitMs = 15;

  assert(resolveAfterBackspaceWaitMs("wayland", plan) == 3);
  assert(resolveAfterBackspaceWaitMs("wayland_v2", plan) == 3);
  assert(resolveAfterBackspaceWaitMs("waylandim", plan) == 3);
  assert(resolveAfterBackspaceWaitMs("waylandfrontend", plan) == 3);
  assert(resolveAfterBackspaceWaitMs("xim", plan) == 10);
  assert(resolveAfterBackspaceWaitMs("fcitx4", plan) == 10);
  assert(resolveAfterBackspaceWaitMs("fcitx4frontend", plan) == 10);
  assert(resolveAfterBackspaceWaitMs("dbus", plan) == 5);
  assert(resolveAfterBackspaceWaitMs("dbusfrontend", plan) == 5);
  assert(resolveAfterBackspaceWaitMs("unknown", plan) == 10);
  assert(resolveAfterBackspaceWaitMs(nullptr, plan) == 10);

  assert(resolveAfterUinputShiftSelectWaitMs("wayland", plan) == 8);
  assert(resolveAfterUinputShiftSelectWaitMs("wayland_v2", plan) == 8);
  assert(resolveAfterUinputShiftSelectWaitMs("waylandim", plan) == 8);
  assert(resolveAfterUinputShiftSelectWaitMs("waylandfrontend", plan) == 8);
  assert(resolveAfterUinputShiftSelectWaitMs("dbus", plan) == 15);
  assert(resolveAfterUinputShiftSelectWaitMs("dbusfrontend", plan) == 15);
  assert(resolveAfterUinputShiftSelectWaitMs("unknown", plan) == 20);
  assert(resolveAfterUinputShiftSelectWaitMs(nullptr, plan) == 20);
}
