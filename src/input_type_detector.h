#pragma once

#include <cstdint>
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
  bool isBrowserUI() const;
  bool isWebContent() const;

  bool inChromiumAddressBar(const fcitx::InputContext &inputContext,
                           const std::string &program = "",
                           uint64_t *addrBarUiVerdictAtUsec = nullptr) const;

private:
  const WindowFocusTracker *tracker_ = nullptr;
  mutable uint64_t fallbackAddrBarUiVerdictAtUsec_ = 0;
};

} // namespace areca
