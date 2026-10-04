#pragma once

#include <string>

namespace areca {

// VS Code and editors built from its codebase.
bool isVSCodeBasedProgram(const std::string &program);

// Programs that need the forward-Backspace compatibility path when they
// expose the otherwise reliable 0x72 capability mask.
bool isVSCodeFamilyProgram(const std::string &program);

// Office programs whose surrounding text is unreliable and must use the
// forward-Backspace compatibility path.
bool requiresForwardBackspaceBackend(const std::string &program);

// Chat applications that need Shift+Left selection for rewrites.
bool requiresShiftSelectBackend(const std::string &program);

// Known Linux terminal applications, excluding KDE terminals.
bool isTerminalProgram(const std::string &program);

// Chromium-family browser applications.
bool isChromiumBrowser(const std::string &program);

// KDE Plasma desktop shell components that must always use forward-Backspace.
bool isPlasmashellProgram(const std::string &program);

} // namespace areca
