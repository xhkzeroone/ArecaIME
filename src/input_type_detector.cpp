#include "input_type_detector.h"

#include <algorithm>
#include <cctype>
#include <string_view>

#include <fcitx-utils/capabilityflags.h>
#include <fcitx/inputcontext.h>

#include "program_compatibility.h"
#include "window_focus_tracker.h"

namespace areca {
namespace {

bool isSdlProgram(std::string program) {
  const auto slash = program.find_last_of("/\\");
  if (slash != std::string::npos) {
    program.erase(0, slash + 1);
  }
  std::transform(
      program.begin(), program.end(), program.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  constexpr std::string_view desktopSuffix = ".desktop";
  if (program.ends_with(desktopSuffix)) {
    program.resize(program.size() - desktopSuffix.size());
  }
  return program.starts_with("sdl");
}

} // namespace

InputTypeDetector::InputTypeDetector(const WindowFocusTracker *tracker)
    : tracker_(tracker) {}

void InputTypeDetector::setFocusTracker(const WindowFocusTracker *tracker) {
  tracker_ = tracker;
}

bool InputTypeDetector::isBrowser(const std::string &program) const {
  if (!program.empty()) {
    return isBrowserLikeProgram(program);
  }
  if (tracker_ && tracker_->isValid()) {
    const std::string tracked = tracker_->focusProgram();
    if (!tracked.empty()) {
      return isBrowserLikeProgram(tracked);
    }
  }
  return true;
}

bool InputTypeDetector::isChromiumBrowser(const std::string &program) const {
  if (!program.empty()) {
    return areca::isChromiumBrowser(program);
  }
  if (tracker_ && tracker_->isValid()) {
    const std::string tracked = tracker_->focusProgram();
    if (!tracked.empty()) {
      return areca::isChromiumBrowser(tracked);
    }
  }
  return false;
}

bool InputTypeDetector::isTerminal(const std::string &program,
                                   const char *frontend) const {
  if (isTerminalProgram(program)) {
    return true;
  }
  if (tracker_ && tracker_->isValid() && tracker_->isTerminalFocused()) {
    if (!program.empty() && isBrowserLikeProgram(program)) {
      return false;
    }
    return true;
  }
  return program.empty() && frontend &&
         std::string_view(frontend).starts_with("dbus");
}

bool InputTypeDetector::canForwardBackspaces(
    const fcitx::InputContext &inputContext, const std::string &program) const {
  const char *frontendCStr = inputContext.frontend();
  const std::string_view frontend = frontendCStr ? frontendCStr : "";

  std::string prog = !program.empty() ? program : inputContext.program();
  if (prog.empty() && tracker_ && tracker_->isValid()) {
    prog = tracker_->focusProgram();
  }

  // SDL applications commonly render and process input themselves. Their
  // frontend may still be Wayland, XIM or IBus, so this must precede the
  // frontend allowlist and force a hardware-backed rewrite path.
  if (isSdlProgram(prog)) {
    return false;
  }

  if (frontend.starts_with("wayland") || frontend == "xim" ||
      frontend.starts_with("ibus")) {
    return true;
  }

  const auto capabilities = inputContext.capabilityFlags();
  const bool hasKeyEventOrderFix =
      capabilities.test(fcitx::CapabilityFlag::KeyEventOrderFix);
  const bool hasSurrounding =
      capabilities.test(fcitx::CapabilityFlag::SurroundingText);

  if (frontend.starts_with("dbus")) {
    return hasKeyEventOrderFix || hasSurrounding;
  }

  if (frontend.starts_with("fcitx4")) {
    const bool hasFormattedPreedit =
        capabilities.test(fcitx::CapabilityFlag::FormattedPreedit);
    return hasFormattedPreedit || hasSurrounding;
  }

  return false;
}

bool InputTypeDetector::inChromiumAddressBar(
    const fcitx::InputContext &inputContext, const std::string &program) const {
  // Url is authoritative. AT-SPI only fills the gap for Chromium frontends
  // that do not expose CapabilityFlag::Url.
  if (inputContext.capabilityFlags().test(fcitx::CapabilityFlag::Url)) {
    return true;
  }
  if (!tracker_ || !tracker_->isValid()) return false;
  const auto focus = tracker_->browserFocus();
  std::string prog = program.empty() ? inputContext.program() : program;
  if (prog.empty()) prog = focus.program;
  return isChromiumBrowser(prog) &&
         isChromiumBrowser(focus.program) &&
         focus.className.find("Omnibox") != std::string::npos;
}

} // namespace areca
