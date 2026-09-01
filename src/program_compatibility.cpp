#include "program_compatibility.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

namespace areca {
namespace {

std::string normalizedProgramName(std::string program) {
  const auto slash = program.find_last_of('/');
  if (slash != std::string::npos) {
    program.erase(0, slash + 1);
  }
  std::transform(
      program.begin(), program.end(), program.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  constexpr std::string_view desktopSuffix = ".desktop";
  if (program.ends_with(desktopSuffix)) {
    program.resize(program.size() - desktopSuffix.size());
  }
  return program;
}

bool isProgrammingProgram(const std::string &program) {
  // IDEs and code editors. Both executable names and desktop/application IDs
  // are included because input-method frontends may report either form.
  static constexpr auto editorsAndIdes = std::to_array<std::string_view>(
      {// JetBrains IDEs and remote-development clients.
       "idea", "idea.sh", "intellij-idea", "intellij-idea-community",
       "intellij-idea-ultimate", "pycharm", "pycharm.sh", "pycharm-community",
       "pycharm-professional", "clion", "clion.sh", "goland", "goland.sh",
       "webstorm", "webstorm.sh", "phpstorm", "phpstorm.sh", "rubymine",
       "rubymine.sh", "rider", "rider.sh", "datagrip", "datagrip.sh",
       "dataspell", "dataspell.sh", "rustrover", "rustrover.sh", "aqua",
       "aqua.sh", "fleet", "jetbrains-fleet", "jetbrains-client",
       "jetbrains-gateway", "gateway", "android-studio", "studio.sh",
       "jetbrains-studio", "com.google.androidstudio",

       // Cross-platform IDEs and modern editors.
       "eclipse", "org.eclipse.eclipse", "netbeans", "apache-netbeans",
       "org.apache.netbeans", "sublime_text", "sublime-text", "subl",
       "com.sublimetext.three", "com.sublimetext.four", "zed", "zed-editor",
       "dev.zed.zed", "lapce", "dev.lapce.lapce", "pulsar",
       "dev.pulsar-edit.pulsar", "atom", "io.atom.atom", "lite-xl",
       "com.lite_xl.lite_xl", "theia", "qoder", "brackets",
       "io.brackets.brackets", "phcode", "io.phcode.phoenix", "codelite",
       "org.codelite.codelite", "monodevelop", "com.xamarin.monodevelop",
       "lazarus", "org.lazarus_ide.lazarus", "eric", "eric6", "eric7", "wing",
       "wingpro", "wing-personal", "komodo", "komodo-ide", "komodo-edit",
       "gnatstudio", "com.adacore.gnatstudio", "qtdesignstudio", "drracket",
       "org.racket_lang.drracket", "bluej", "org.bluej.bluej", "greenfoot",
       "processing", "org.processing.processingide",

       // Linux desktop editors and IDEs.
       "gnome-builder", "org.gnome.builder", "geany", "org.geany.geany",
       "qtcreator",
       "qt-creator", "org.qt-project.qtcreator", "codeblocks", "anjuta",
       "org.gnome.anjuta", "bluefish", "nl.openoffice.bluefish", "cudatext",
       "io.github.cudatext.cudatext", "liteide", "notepadqq",
       "com.notepadqq.notepadqq", "juffed", "scite", "medit", "textadept",

       // Graphical Vim/Neovim and Emacs clients.
       "gvim", "vim-gtk", "vim-gtk3", "neovide", "com.neovide.neovide",
       "goneovim", "com.github.akiyosi.goneovim", "nvim-qt",
       "io.github.nvim-qt", "fvim", "gnvim", "nyaovim", "uivonim", "oni",
       "oni2", "onivim2", "emacs", "emacsclient", "emacs-gtk", "emacs-pgtk"});

  static constexpr auto specializedIdes = std::to_array<std::string_view>(
      {// Data-science and numerical-computing IDEs.
       "rstudio", "rstudio-bin", "org.rstudio.rstudio", "spyder",
       "org.spyder-ide.spyder", "thonny", "org.thonny.thonny", "idle", "idle3",
       "jupyterlab-desktop", "org.jupyter.jupyterlab-desktop", "matlab",
       "octave", "octave-gui", "org.octave.octave", "scilab",
       "org.scilab.scilab", "mathematica", "wolfram",

       // Embedded, electronics and hardware-development IDEs.
       "arduino", "arduino-ide", "cc.arduino.ide2", "mplab_ide", "mplabx",
       "stm32cubeide", "ccstudio", "codecomposerstudio", "emstudio",
       "mcuxpressoide", "e2studio", "modus-toolbox", "vitis", "vivado",
       "quartus", "quartusii", "lattice-radiant", "nsight", "nsight-eclipse",
       "kicad", "org.kicad.kicad",

       // Game-engine editors with integrated scripting environments.
       "godot", "godot-editor", "org.godotengine.godot", "unity",
       "unity-editor", "unityhub", "com.unity.unityhub", "ue4editor",
       "unrealeditor", "unreal-editor", "gamemaker", "gamemakerstudio",
       "defold", "com.defold.editor", "gdevelop", "io.gdevelop.ide", "renpy",
       "org.renpy.renpy", "flutterflow", "io.flutterflow.desktop"});

  static constexpr auto developerTools = std::to_array<std::string_view>(
      {// Database, API and version-control tools commonly used for development.
       "dbeaver", "dbeaver-ce", "io.dbeaver.dbeavercommunity",
       "beekeeper-studio", "io.beekeeperstudio.studio", "dbgate",
       "org.dbgate.dbgate", "mysql-workbench", "com.mysql.workbench",
       "pgadmin4", "org.pgadmin.pgadmin4", "mongodb-compass", "redisinsight",
       "azuredatastudio", "sqldeveloper", "oracle-sqldeveloper",
       "sqlitebrowser", "org.sqlitebrowser.sqlitebrowser", "tableplus",
       "postman", "com.getpostman.postman", "insomnia",
       "rest.insomnia.insomnia", "bruno", "com.usebruno.bruno", "httpie",
       "io.httpie.desktop", "altair", "com.altairgraphql.altair", "hoppscotch",
       "yaak", "com.yaak.app", "gitkraken", "com.axosoft.gitkraken", "smartgit",
       "com.syntevo.smartgit", "git-cola", "com.github.git-cola.git-cola",
       "gitg", "org.gnome.gitg", "github-desktop", "io.github.shiftey.desktop",
       "sublime_merge", "sublime-merge", "gitbutler", "com.gitbutler.gitbutler",
       "gitfiend", "gitahead", "meld", "org.gnome.meld", "bcompare",

       // Container and cluster desktop environments used during development.
       "docker-desktop", "com.docker.desktop", "podman-desktop",
       "io.podman_desktop.podmandesktop", "rancher-desktop",
       "io.rancherdesktop.app", "lens-desktop", "openlens",
       "dev.k8slens.openlens"});

  const auto isExactMatch = [&program](const auto &names) {
    return std::find(names.begin(), names.end(), program) != names.end();
  };
  if (isExactMatch(editorsAndIdes) || isExactMatch(specializedIdes) ||
      isExactMatch(developerTools)) {
    return true;
  }

  static constexpr auto prefixes =
      std::to_array<std::string_view>({"jetbrains-",
                                       "com.jetbrains.",
                                       "intellij-idea-",
                                       "pycharm-",
                                       "android-studio-",
                                       "com.google.androidstudio.",
                                       "eclipse-",
                                       "org.eclipse.eclipse.",
                                       "apache-netbeans-",
                                       "dev.zed.zed.",
                                       "zed-preview-",
                                       "zed-editor-",
                                       "dev.lapce.lapce.",
                                       "org.gnome.builder.",
                                       "qtcreator-",
                                       "emacs-",
                                       "idle-python",
                                       "arduino-ide-",
                                       "stm32cubeide-",
                                       "mcuxpressoide-",
                                       "e2studio-",
                                       "vitis-",
                                       "vivado-",
                                       "quartus-",
                                       "org.godotengine.godot.",
                                       "unity-editor-",
                                       "unrealeditor-",
                                       "dbeaver-",
                                       "docker-desktop-",
                                       "podman-desktop-",
                                       "rancher-desktop-"});
  return std::any_of(prefixes.begin(), prefixes.end(),
                     [&program](std::string_view prefix) {
                       return program.starts_with(prefix);
                     });
}

} // namespace

bool isMicrosoftEdgeProgram(const std::string &rawProgram) {
  const std::string program = normalizedProgramName(rawProgram);
  return program == "microsoft-edge" ||
         program.starts_with("microsoft-edge-") ||
         program == "com.microsoft.edge" ||
         program.starts_with("com.microsoft.edge.");
}

bool isTerminalProgram(const std::string &rawProgram) {
  const std::string program = normalizedProgramName(rawProgram);
  static constexpr auto distroTerminals =
      std::to_array<std::string_view>({"gnome-terminal",
                                       "gnome-terminal-server",
                                       "gnome-terminal.real",
                                       "org.gnome.terminal",
                                       "kgx",
                                       "org.gnome.console",
                                       "ptyxis",
                                       "app.devsuite.ptyxis",
                                       "xfce4-terminal",
                                       "org.xfce.terminal",
                                       "mate-terminal",
                                       "org.mate.terminal",
                                       "lxterminal",
                                       "qterminal",
                                       "org.lxqt.qterminal",
                                       "deepin-terminal",
                                       "com.deepin.terminal",
                                       "pantheon-terminal",
                                       "io.elementary.terminal",
                                       "cosmic-term",
                                       "com.system76.cosmicterm",
                                       "cutefish-terminal",
                                       "com.cutefish.terminal",
                                       "ukui-terminal",
                                       "kylin-terminal",
                                       "terminology",
                                       "sakura",
                                       "lilyterm",
                                       "roxterm",
                                       "vala-terminal",
                                       "eterm",
                                       "mlterm",
                                       "xterm",
                                       "uxterm",
                                       "rxvt",
                                       "urxvt",
                                       "zutty",
                                       "x-terminal-emulator"});

  static constexpr auto thirdPartyTerminals =
      std::to_array<std::string_view>({"alacritty",
                                       "io.alacritty.alacritty",
                                       "kitty",
                                       "net.kovidgoyal.kitty",
                                       "wezterm",
                                       "org.wezfurlong.wezterm",
                                       "foot",
                                       "footclient",
                                       "st",
                                       "tilix",
                                       "terminix",
                                       "com.gexperts.tilix",
                                       "terminator",
                                       "net.launchpad.terminator",
                                       "guake",
                                       "org.guake.guake",
                                       "tilda",
                                       "blackbox",
                                       "com.raggesilver.blackbox",
                                       "ghostty",
                                       "com.mitchellh.ghostty",
                                       "warp-terminal",
                                       "dev.warp.warp",
                                       "tabby",
                                       "tabby-terminal",
                                       "org.tabby",
                                       "hyper",
                                       "co.zeit.hyper",
                                       "rio",
                                       "com.raphaelamorim.rio",
                                       "contour",
                                       "org.contourterminal.contour",
                                       "waveterm",
                                       "dev.waveterm.wave",
                                       "extraterm",
                                       "com.extraterm.extraterm",
                                       "termius-app",
                                       "com.termius.termius",
                                       "cool-retro-term",
                                       "org.cool-retro-term",
                                       "rxvt-unicode",
                                       "urxvtc",
                                       "urxvtd"});

  const auto isExactMatch = [&program](const auto &names) {
    return std::find(names.begin(), names.end(), program) != names.end();
  };
  if (isExactMatch(distroTerminals) || isExactMatch(thirdPartyTerminals)) {
    return true;
  }

  static constexpr auto prefixes = std::to_array<std::string_view>(
      {"org.gnome.terminal.", "org.gnome.console.", "app.devsuite.ptyxis.",
       "org.xfce.terminal.", "org.mate.terminal.", "com.deepin.terminal.",
       "io.elementary.terminal.", "com.system76.cosmicterm.",
       "warp-terminal-", "dev.warp.warp-", "wezterm-", "kitty-"});
  return std::any_of(prefixes.begin(), prefixes.end(),
                     [&program](std::string_view prefix) {
                       return program.starts_with(prefix);
                     });
}

bool isVSCodeFamilyProgram(const std::string &rawProgram) {
  const std::string program = normalizedProgramName(rawProgram);
  if (program == "code" || program.starts_with("code-")) {
    return true;
  }

  static constexpr std::array<std::string_view, 10> markers = {
      "visual-studio-code", "visualstudio.code", "vscode",   "codium", "cursor",
      "windsurf",           "antigravity",       "positron", "pearai", "trae"};
  const bool isVSCodeFamily =
      std::any_of(markers.begin(), markers.end(),
                  [&program](std::string_view marker) {
                    return program.find(marker) != std::string::npos;
                  }) ||
      program == "kiro" || program.starts_with("kiro-") || program == "void" ||
      program.starts_with("void-");
  return isVSCodeFamily || isProgrammingProgram(program) ||
         isTerminalProgram(program);
}

} // namespace areca
