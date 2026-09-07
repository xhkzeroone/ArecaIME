#pragma once

#include <string>

namespace areca {

class WindowFocusTracker;

class InputTypeDetector {
public:
  explicit InputTypeDetector(const WindowFocusTracker *tracker = nullptr);

  void setFocusTracker(const WindowFocusTracker *tracker);

  bool isTerminal(const std::string &program, const char *frontend = nullptr) const;
  bool isBrowser(const std::string &program) const;
  bool isBrowserUI() const;
  bool isWebContent() const;

private:
  const WindowFocusTracker *tracker_ = nullptr;
};

} // namespace areca
