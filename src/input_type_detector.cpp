#include "input_type_detector.h"

#include <string_view>

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

} // namespace areca
