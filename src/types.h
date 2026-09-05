#pragma once

#include <cstdint>
#include <string>

namespace areca {

struct BambooResult {
  std::string currentText;
  std::string newText;
  uint32_t deleteCount = 0;
  std::string commitText;
  bool macroExpanded = false;
};

struct RewritePlan {
  uint64_t transactionId = 0;
  uint32_t backspaceCount = 0;
  uint32_t backspaceDelayMs = 1;
  uint32_t afterBackspaceWaitMs = 10;
  uint32_t waylandAfterBackspaceWaitMs = 3;
  uint32_t ximAfterBackspaceWaitMs = 10;
  uint32_t fcitx4AfterBackspaceWaitMs = 10;
  uint32_t dbusAfterBackspaceWaitMs = 20;

  uint32_t uinputShiftSelectDelayMs = 1;
  uint32_t afterUinputShiftSelectWaitMs = 10;
  uint32_t waylandAfterUinputShiftSelectWaitMs = 3;
  uint32_t ximAfterUinputShiftSelectWaitMs = 10;
  uint32_t fcitx4AfterUinputShiftSelectWaitMs = 10;
  uint32_t dbusAfterUinputShiftSelectWaitMs = 20;

  uint64_t timerAccuracyUsec = 1;
  std::string commitText;
};

inline uint32_t resolveAfterBackspaceWaitMs(const char *frontendName,
                                            const RewritePlan &plan) {
  if (!frontendName) {
    return plan.afterBackspaceWaitMs;
  }
  const std::string_view fe(frontendName);
  if (fe.starts_with("wayland")) {
    return plan.waylandAfterBackspaceWaitMs;
  }
  if (fe.starts_with("xim")) {
    return plan.ximAfterBackspaceWaitMs;
  }
  if (fe.starts_with("fcitx4")) {
    return plan.fcitx4AfterBackspaceWaitMs;
  }
  if (fe.starts_with("dbus")) {
    return plan.dbusAfterBackspaceWaitMs;
  }
  return plan.afterBackspaceWaitMs;
}

inline uint32_t resolveAfterUinputShiftSelectWaitMs(const char *frontendName,
                                                     const RewritePlan &plan) {
  if (!frontendName) {
    return plan.afterUinputShiftSelectWaitMs;
  }
  const std::string_view fe(frontendName);
  if (fe.starts_with("wayland")) {
    return plan.waylandAfterUinputShiftSelectWaitMs;
  }
  if (fe.starts_with("xim")) {
    return plan.ximAfterUinputShiftSelectWaitMs;
  }
  if (fe.starts_with("fcitx4")) {
    return plan.fcitx4AfterUinputShiftSelectWaitMs;
  }
  if (fe.starts_with("dbus")) {
    return plan.dbusAfterUinputShiftSelectWaitMs;
  }
  return plan.afterUinputShiftSelectWaitMs;
}

} // namespace areca
