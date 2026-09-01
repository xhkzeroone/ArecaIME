#pragma once

#include <string>

namespace areca {

// Programs that need the forward-Backspace compatibility path when they
// expose the otherwise reliable 0x72 capability mask.
bool isVSCodeFamilyProgram(const std::string &program);

// Microsoft Edge is temporarily routed through uinput for compatibility tests.
bool isMicrosoftEdgeProgram(const std::string &program);

// Known Linux terminal applications, excluding KDE terminals.
bool isTerminalProgram(const std::string &program);

} // namespace areca
