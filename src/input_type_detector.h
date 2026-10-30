#pragma once

#include <string>

namespace fcitx {
class InputContext;
}

namespace areca {

class WindowFocusTracker;

class InputTypeDetector {
public:
  explicit InputTypeDetector(const WindowFocusTracker *tracker = nullptr);

  void setFocusTracker(const WindowFocusTracker *tracker);

  bool isTerminal(const std::string &program, const char *frontend = nullptr) const;
  bool isBrowser(const std::string &program) const;
  bool isChromiumBrowser(const std::string &program) const;
  bool canForwardBackspaces(const fcitx::InputContext &inputContext,
                           const std::string &program = "") const;

  bool inChromiumAddressBar(const fcitx::InputContext &inputContext,
                           const std::string &program = "") const;

private:
  const WindowFocusTracker *tracker_ = nullptr;
};

} // namespace areca
