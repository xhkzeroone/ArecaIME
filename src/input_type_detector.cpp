#include "input_type_detector.h"

#include <string_view>

#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/event.h>
#include <fcitx/inputcontext.h>

#include "browser_autocomplete.h"
#include "program_compatibility.h"
#include "window_focus_tracker.h"

namespace areca {

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
    return true;
  }
  return program.empty() && frontend &&
         std::string_view(frontend).starts_with("dbus");
}

bool InputTypeDetector::isBrowserUI() const {
  return tracker_ && tracker_->isValid() && tracker_->isBrowserUIFocused();
}

bool InputTypeDetector::isWebContent() const {
  return tracker_ && tracker_->isValid() && tracker_->isWebContentFocused();
}

bool InputTypeDetector::inChromiumAddressBar(
    const fcitx::InputContext &inputContext,
    const std::string &program,
    uint64_t *addrBarUiVerdictAtUsec) const {
  if (inputContext.capabilityFlags().test(fcitx::CapabilityFlag::Url)) {
    return true;
  }

  std::string prog = !program.empty() ? program : inputContext.program();
  if (prog.empty() && tracker_ && tracker_->isValid()) {
    prog = tracker_->focusProgram();
  }

  if (isChromiumBrowser(prog)) {
    uint64_t &verdictTime = addrBarUiVerdictAtUsec
                                ? *addrBarUiVerdictAtUsec
                                : fallbackAddrBarUiVerdictAtUsec_;

    if (tracker_ && tracker_->isValid() && tracker_->isBrowserUIFocused()) {
      verdictTime = fcitx::now(CLOCK_MONOTONIC);
      return true;
    }

    const uint64_t nowUsec = fcitx::now(CLOCK_MONOTONIC);
    if (verdictTime != 0 && nowUsec - verdictTime <= 5000000) {
      return true;
    }
    verdictTime = 0;

    const auto &rect = inputContext.cursorRect();
    if (rect.width() <= 2 && rect.height() >= 18 && rect.height() <= 24 &&
        rect.top() >= 0 && rect.top() < 200) {
      return true;
    }
  }

  return false;
}

} // namespace areca
