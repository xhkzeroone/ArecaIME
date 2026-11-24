#include "program_compatibility.h"
#include "types.h"

#include <cassert>

int main() {
  using areca::isVSCodeBasedProgram;
  using areca::isVSCodeFamilyProgram;

  // Existing VS Code-family matching remains intact.
  assert(isVSCodeBasedProgram("code"));
  assert(isVSCodeBasedProgram("/usr/bin/code-insiders"));
  assert(isVSCodeBasedProgram("VSCodium.desktop"));
  assert(isVSCodeBasedProgram("cursor"));
  assert(isVSCodeBasedProgram("windsurf"));
  assert(!isVSCodeBasedProgram("gnome-terminal-server"));
  assert(!isVSCodeBasedProgram("jetbrains-idea-ultimate.desktop"));
  assert(!isVSCodeBasedProgram("firefox"));

  assert(isVSCodeFamilyProgram("code"));
  assert(isVSCodeFamilyProgram("/usr/bin/code-insiders"));
  assert(isVSCodeFamilyProgram("VSCodium.desktop"));
  assert(isVSCodeFamilyProgram("cursor"));
  assert(isVSCodeFamilyProgram("windsurf"));

  // Non-VS Code programs are excluded from isVSCodeFamilyProgram
  assert(!isVSCodeFamilyProgram("gnome-terminal-server"));
  assert(!isVSCodeFamilyProgram("jetbrains-idea-ultimate.desktop"));
  assert(!isVSCodeFamilyProgram("firefox"));

  using areca::isProgrammingProgram;
  // General-purpose IDEs, code editors and their package-channel app IDs.
  assert(isProgrammingProgram("jetbrains-idea-ultimate.desktop"));
  assert(isProgrammingProgram("com.jetbrains.PyCharm-Professional.desktop"));
  assert(isProgrammingProgram("com.google.AndroidStudio.desktop"));
  assert(isProgrammingProgram("org.eclipse.Eclipse"));
  assert(isProgrammingProgram("dev.zed.Zed.desktop"));
  assert(isProgrammingProgram("org.gnome.Builder.Devel.desktop"));
  assert(isProgrammingProgram("/usr/bin/neovide"));
  assert(isProgrammingProgram("neovide.bin"));
  assert(isProgrammingProgram("eclipse-beta.desktop"));
  assert(isProgrammingProgram("plasmashell"));
  assert(isProgrammingProgram("org.kde.plasmashell.desktop"));

  // Specialized IDEs and other development tools.
  assert(isProgrammingProgram("org.spyder-ide.spyder"));
  assert(isProgrammingProgram("cc.arduino.IDE2.desktop"));
  assert(isProgrammingProgram("org.godotengine.Godot"));
  assert(isProgrammingProgram("io.dbeaver.DBeaverCommunity.desktop"));
  assert(isProgrammingProgram("com.getpostman.Postman"));
  assert(isProgrammingProgram("com.axosoft.GitKraken.desktop"));

  // Exact matching prevents unrelated applications from inheriting the rule.
  assert(!isProgrammingProgram("terminal-notes"));
  assert(!isProgrammingProgram("wavebox"));
  assert(!isProgrammingProgram("footage"));
  assert(!isProgrammingProgram("stationeers"));
  assert(!isProgrammingProgram("studio-one"));
  assert(!isProgrammingProgram("atomizer"));
  assert(!isProgrammingProgram("firefox"));
  assert(!isProgrammingProgram(""));

  using areca::isOfficeProgram;
  assert(isOfficeProgram("libreoffice"));
  assert(isOfficeProgram("/usr/bin/soffice"));
  assert(isOfficeProgram("/usr/lib/libreoffice/program/soffice.bin"));
  assert(isOfficeProgram("softoffice.bin"));
  assert(isOfficeProgram("libreoffice-writer.desktop"));
  assert(isOfficeProgram("org.libreoffice.LibreOffice.desktop"));
  assert(isOfficeProgram("DesktopEditors"));
  assert(isOfficeProgram("onlyoffice-desktopeditors.desktop"));
  assert(isOfficeProgram("org.onlyoffice.desktopeditors"));
  assert(isOfficeProgram("libreoffice-writer-beta.desktop"));
  assert(isOfficeProgram("onlyoffice-desktopeditors-preview.desktop"));
  assert(!isOfficeProgram("openoffice"));
  assert(!isOfficeProgram("office-notes"));
  assert(!isOfficeProgram("libreofficetest"));
  assert(!isOfficeProgram("sofficehelper"));
  assert(!isOfficeProgram("onlyofficetest"));
  assert(!isOfficeProgram(""));

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
  assert(isTerminalProgram("org.gnome.Terminal.Devel.desktop"));
  assert(isTerminalProgram("ghostty-beta.desktop"));
  assert(isTerminalProgram("foot.bin"));
  assert(!isTerminalProgram("footage"));
  assert(!isTerminalProgram("stationeers"));
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

  using areca::isBrowserLikeProgram;
  assert(isBrowserLikeProgram("google-chrome"));
  assert(isBrowserLikeProgram("google-chrome.desktop"));
  assert(isBrowserLikeProgram("/usr/bin/firefox"));
  assert(isBrowserLikeProgram("/usr/bin/firefox.desktop"));
  assert(isBrowserLikeProgram("thorium"));
  assert(isBrowserLikeProgram("ladybird"));
  assert(isBrowserLikeProgram("min"));
  assert(isBrowserLikeProgram("luakit"));
  assert(isBrowserLikeProgram(""));
  assert(!isBrowserLikeProgram("org.kde.kate"));

  using areca::resolveAfterBackspaceWaitMs;
  using areca::resolveAfterNativeBackspaceWaitMs;
  using areca::resolveAfterUinputShiftSelectWaitMs;
  areca::RewritePlan plan;
  assert(plan.nativeBackspaceDelayMs == 1);
  assert(plan.afterNativeBackspaceWaitMs == 10);
  assert(plan.waylandAfterNativeBackspaceWaitMs == 10);
  assert(plan.ximAfterNativeBackspaceWaitMs == 10);
  assert(plan.fcitx4AfterNativeBackspaceWaitMs == 10);
  assert(plan.dbusAfterNativeBackspaceWaitMs == 10);
  assert(plan.ibusAfterNativeBackspaceWaitMs == 10);
  assert(plan.surroundingWaitMs == 3);
  assert(plan.dbusAfterBackspaceWaitMs == 10);
  assert(plan.ibusAfterBackspaceWaitMs == 10);
  assert(plan.afterUinputShiftSelectWaitMs == 5);
  assert(plan.waylandAfterUinputShiftSelectWaitMs == 5);
  assert(plan.ximAfterUinputShiftSelectWaitMs == 5);
  assert(plan.fcitx4AfterUinputShiftSelectWaitMs == 5);
  assert(plan.dbusAfterUinputShiftSelectWaitMs == 5);
  assert(plan.ibusAfterUinputShiftSelectWaitMs == 5);
  plan.afterBackspaceWaitMs = 10;
  plan.waylandAfterBackspaceWaitMs = 3;
  plan.ximAfterBackspaceWaitMs = 10;
  plan.fcitx4AfterBackspaceWaitMs = 10;
  plan.dbusAfterBackspaceWaitMs = 5;
  plan.ibusAfterBackspaceWaitMs = 7;

  plan.afterUinputShiftSelectWaitMs = 20;
  plan.waylandAfterUinputShiftSelectWaitMs = 8;
  plan.ximAfterUinputShiftSelectWaitMs = 20;
  plan.fcitx4AfterUinputShiftSelectWaitMs = 20;
  plan.dbusAfterUinputShiftSelectWaitMs = 15;
  plan.ibusAfterUinputShiftSelectWaitMs = 17;

  assert(resolveAfterBackspaceWaitMs("wayland", plan) == 3);
  assert(resolveAfterBackspaceWaitMs("wayland_v2", plan) == 3);
  assert(resolveAfterBackspaceWaitMs("waylandim", plan) == 3);
  assert(resolveAfterBackspaceWaitMs("waylandfrontend", plan) == 3);
  assert(resolveAfterBackspaceWaitMs("xim", plan) == 10);
  assert(resolveAfterBackspaceWaitMs("fcitx4", plan) == 10);
  assert(resolveAfterBackspaceWaitMs("fcitx4frontend", plan) == 10);
  assert(resolveAfterBackspaceWaitMs("dbus", plan) == 5);
  assert(resolveAfterBackspaceWaitMs("dbusfrontend", plan) == 5);
  assert(resolveAfterBackspaceWaitMs("ibus", plan) == 7);
  assert(resolveAfterBackspaceWaitMs("ibusfrontend", plan) == 7);
  assert(resolveAfterBackspaceWaitMs("unknown", plan) == 10);
  assert(resolveAfterBackspaceWaitMs(nullptr, plan) == 10);

  assert(resolveAfterUinputShiftSelectWaitMs("wayland", plan) == 8);
  assert(resolveAfterUinputShiftSelectWaitMs("wayland_v2", plan) == 8);
  assert(resolveAfterUinputShiftSelectWaitMs("waylandim", plan) == 8);
  assert(resolveAfterUinputShiftSelectWaitMs("waylandfrontend", plan) == 8);
  assert(resolveAfterUinputShiftSelectWaitMs("dbus", plan) == 15);
  assert(resolveAfterUinputShiftSelectWaitMs("dbusfrontend", plan) == 15);
  assert(resolveAfterUinputShiftSelectWaitMs("ibus", plan) == 17);
  assert(resolveAfterUinputShiftSelectWaitMs("ibusfrontend", plan) == 17);
  assert(resolveAfterUinputShiftSelectWaitMs("unknown", plan) == 20);
  assert(resolveAfterUinputShiftSelectWaitMs(nullptr, plan) == 20);

  plan.afterNativeBackspaceWaitMs = 12;
  plan.waylandAfterNativeBackspaceWaitMs = 4;
  plan.ximAfterNativeBackspaceWaitMs = 14;
  plan.fcitx4AfterNativeBackspaceWaitMs = 11;
  plan.dbusAfterNativeBackspaceWaitMs = 6;
  plan.ibusAfterNativeBackspaceWaitMs = 9;

  assert(resolveAfterNativeBackspaceWaitMs("wayland", plan) == 4);
  assert(resolveAfterNativeBackspaceWaitMs("wayland_v2", plan) == 4);
  assert(resolveAfterNativeBackspaceWaitMs("waylandim", plan) == 4);
  assert(resolveAfterNativeBackspaceWaitMs("xim", plan) == 14);
  assert(resolveAfterNativeBackspaceWaitMs("fcitx4", plan) == 11);
  assert(resolveAfterNativeBackspaceWaitMs("dbus", plan) == 6);
  assert(resolveAfterNativeBackspaceWaitMs("ibus", plan) == 9);
  assert(resolveAfterNativeBackspaceWaitMs("ibusfrontend", plan) == 9);
  assert(resolveAfterNativeBackspaceWaitMs("unknown", plan) == 12);
  assert(resolveAfterNativeBackspaceWaitMs(nullptr, plan) == 12);
}
