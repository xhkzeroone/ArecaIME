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

  using areca::isChromiumBrowser;
  assert(isChromiumBrowser("google-chrome"));
  assert(isChromiumBrowser("google-chrome.desktop"));
  assert(isChromiumBrowser("/usr/bin/google-chrome-stable"));
  assert(isChromiumBrowser("chromium"));
  assert(isChromiumBrowser("chromium-browser"));
  assert(isChromiumBrowser("brave"));
  assert(isChromiumBrowser("brave-browser"));
  assert(isChromiumBrowser("microsoft-edge"));
  assert(isChromiumBrowser("microsoft-edge-dev"));
  assert(isChromiumBrowser("edge"));
  assert(isChromiumBrowser("vivaldi"));
  assert(isChromiumBrowser("opera"));
  assert(isChromiumBrowser("coccoc"));
  assert(isChromiumBrowser("coccoc-browser"));
  assert(isChromiumBrowser("yandex"));
  assert(isChromiumBrowser("thorium"));
  assert(!isChromiumBrowser("firefox"));
  assert(!isChromiumBrowser("firefox-esr"));
  assert(!isChromiumBrowser("librewolf"));
  assert(!isChromiumBrowser("code"));
  assert(!isChromiumBrowser("vscode"));
  assert(!isChromiumBrowser("slack"));
  assert(!isChromiumBrowser("ghostty"));
  assert(!isChromiumBrowser(""));

  using areca::isBrowserLikeProgram;
  using areca::isFirefoxFamilyProgram;
  assert(isBrowserLikeProgram("google-chrome.desktop"));
  assert(isBrowserLikeProgram("/usr/bin/firefox"));
  assert(isBrowserLikeProgram(""));
  assert(!isBrowserLikeProgram("org.kde.kate"));
  assert(isFirefoxFamilyProgram("Firefox.desktop"));
  assert(isFirefoxFamilyProgram("org.mozilla.firefoxdeveloperedition"));
  assert(isFirefoxFamilyProgram("librewolf"));
  assert(!isFirefoxFamilyProgram("chromium"));

  using areca::resolveAfterBackspaceWaitMs;
  using areca::resolveAfterUinputShiftSelectWaitMs;
  using areca::resolveAfterXTestBackspaceWaitMs;
  using areca::resolveSurroundingDeleteDelayMs;
  areca::RewritePlan plan;
  assert(plan.xtestBackspaceDelayMs == 1);
  assert(plan.afterXTestBackspaceWaitMs == 10);
  assert(plan.waylandAfterXTestBackspaceWaitMs == 3);
  assert(plan.ximAfterXTestBackspaceWaitMs == 10);
  assert(plan.fcitx4AfterXTestBackspaceWaitMs == 10);
  assert(plan.dbusAfterXTestBackspaceWaitMs == 10);
  assert(plan.ibusAfterXTestBackspaceWaitMs == 10);
  assert(plan.surroundingWaitMs == 3);
  assert(plan.surroundingDeleteDelayMs == 10);
  assert(plan.waylandSurroundingDeleteDelayMs == 0);
  assert(plan.afterSurroundingDeleteWaitMs == 1);
  assert(plan.dbusAfterBackspaceWaitMs == 10);
  assert(plan.ibusAfterBackspaceWaitMs == 10);
  assert(plan.afterUinputShiftSelectWaitMs == 20);
  assert(plan.waylandAfterUinputShiftSelectWaitMs == 20);
  assert(plan.ximAfterUinputShiftSelectWaitMs == 20);
  assert(plan.fcitx4AfterUinputShiftSelectWaitMs == 20);
  assert(plan.dbusAfterUinputShiftSelectWaitMs == 20);
  assert(plan.ibusAfterUinputShiftSelectWaitMs == 20);
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

  assert(resolveSurroundingDeleteDelayMs("wayland", plan) == 0);
  assert(resolveSurroundingDeleteDelayMs("wayland_v2", plan) == 0);
  assert(resolveSurroundingDeleteDelayMs("waylandim", plan) == 0);
  assert(resolveSurroundingDeleteDelayMs("waylandfrontend", plan) == 0);
  assert(resolveSurroundingDeleteDelayMs("dbus", plan) == 10);
  assert(resolveSurroundingDeleteDelayMs("xim", plan) == 10);
  assert(resolveSurroundingDeleteDelayMs("fcitx4", plan) == 10);
  assert(resolveSurroundingDeleteDelayMs("unknown", plan) == 10);
  assert(resolveSurroundingDeleteDelayMs(nullptr, plan) == 10);

  plan.afterXTestBackspaceWaitMs = 12;
  plan.waylandAfterXTestBackspaceWaitMs = 4;
  plan.ximAfterXTestBackspaceWaitMs = 14;
  plan.fcitx4AfterXTestBackspaceWaitMs = 11;
  plan.dbusAfterXTestBackspaceWaitMs = 6;
  plan.ibusAfterXTestBackspaceWaitMs = 9;

  assert(resolveAfterXTestBackspaceWaitMs("wayland", plan) == 4);
  assert(resolveAfterXTestBackspaceWaitMs("wayland_v2", plan) == 4);
  assert(resolveAfterXTestBackspaceWaitMs("waylandim", plan) == 4);
  assert(resolveAfterXTestBackspaceWaitMs("xim", plan) == 14);
  assert(resolveAfterXTestBackspaceWaitMs("fcitx4", plan) == 11);
  assert(resolveAfterXTestBackspaceWaitMs("dbus", plan) == 6);
  assert(resolveAfterXTestBackspaceWaitMs("ibus", plan) == 9);
  assert(resolveAfterXTestBackspaceWaitMs("ibusfrontend", plan) == 9);
  assert(resolveAfterXTestBackspaceWaitMs("unknown", plan) == 12);
  assert(resolveAfterXTestBackspaceWaitMs(nullptr, plan) == 12);
}
