#pragma once

#include <string>

namespace areca {

// Programs that need the forward-Backspace compatibility path when they
// expose the otherwise reliable 0x72 capability mask.
bool isVSCodeFamilyProgram(const std::string &program);

// Known Linux terminal applications, excluding KDE terminals.
bool isTerminalProgram(const std::string &program);

// Chromium-family browser applications.
bool isChromiumBrowser(const std::string &program);

} // namespace areca
