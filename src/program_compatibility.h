#pragma once

#include <string>

namespace areca {

// VS Code and editors built from its codebase.
bool isVSCodeBasedProgram(const std::string &program);

// Programs that need the forward-Backspace compatibility path when they
// expose the otherwise reliable 0x72 capability mask (strictly VS Code family).
bool isVSCodeFamilyProgram(const std::string &program);

// Programming editors and IDEs (JetBrains, Sublime Text, Zed, Eclipse, etc.).
bool isProgrammingProgram(const std::string &program);

// Office suites that need the forward-Backspace compatibility path.
bool isOfficeProgram(const std::string &program);

// Chat applications that need Shift+Left selection for rewrites.
bool requiresShiftSelectBackend(const std::string &program);

// Known Linux terminal applications, excluding KDE terminals.
bool isTerminalProgram(const std::string &program);

// Chromium-family browser applications.
bool isChromiumBrowser(const std::string &program);

// Browser-family application detection shared by input routing and browser
// autocomplete handling.
bool isBrowserLikeProgram(const std::string &program);
bool isFirefoxFamilyProgram(const std::string &program);

} // namespace areca
