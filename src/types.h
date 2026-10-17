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
  uint32_t dbusAfterBackspaceWaitMs = 10;
  uint32_t ibusAfterBackspaceWaitMs = 10;

  uint32_t uinputShiftSelectDelayMs = 1;
  uint32_t afterUinputShiftSelectWaitMs = 20;
  uint32_t waylandAfterUinputShiftSelectWaitMs = 20;
  uint32_t ximAfterUinputShiftSelectWaitMs = 20;
  uint32_t fcitx4AfterUinputShiftSelectWaitMs = 20;
  uint32_t dbusAfterUinputShiftSelectWaitMs = 20;
  uint32_t ibusAfterUinputShiftSelectWaitMs = 20;

  uint32_t surroundingWaitMs = 3;
  uint32_t surroundingDeleteDelayMs = 10;
  uint32_t waylandSurroundingDeleteDelayMs = 0;
  uint32_t afterSurroundingDeleteWaitMs = 1;
  uint32_t xtestBackspaceDelayMs = 1;
  uint32_t afterXTestBackspaceWaitMs = 10;
  uint32_t waylandAfterXTestBackspaceWaitMs = 3;
  uint32_t ximAfterXTestBackspaceWaitMs = 10;
  uint32_t fcitx4AfterXTestBackspaceWaitMs = 10;
  uint32_t dbusAfterXTestBackspaceWaitMs = 10;
  uint32_t ibusAfterXTestBackspaceWaitMs = 10;

  uint64_t timerAccuracyUsec = 1;
  std::string commitText;
  bool probeSurrounding = false;
};

inline uint32_t resolveAfterXTestBackspaceWaitMs(const char *frontendName,
                                                 const RewritePlan &plan) {
  if (!frontendName) {
    return plan.afterXTestBackspaceWaitMs;
  }
  const std::string_view fe(frontendName);
  if (fe.starts_with("wayland")) {
    return plan.waylandAfterXTestBackspaceWaitMs;
  }
  if (fe.starts_with("xim")) {
    return plan.ximAfterXTestBackspaceWaitMs;
  }
  if (fe.starts_with("fcitx4")) {
    return plan.fcitx4AfterXTestBackspaceWaitMs;
  }
  if (fe.starts_with("dbus")) {
    return plan.dbusAfterXTestBackspaceWaitMs;
  }
  if (fe.starts_with("ibus")) {
    return plan.ibusAfterXTestBackspaceWaitMs;
  }
  return plan.afterXTestBackspaceWaitMs;
}

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
  if (fe.starts_with("ibus")) {
    return plan.ibusAfterBackspaceWaitMs;
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
  if (fe.starts_with("ibus")) {
    return plan.ibusAfterUinputShiftSelectWaitMs;
  }
  return plan.afterUinputShiftSelectWaitMs;
}

inline uint32_t resolveSurroundingDeleteDelayMs(const char *frontendName,
                                                const RewritePlan &plan) {
  if (!frontendName) {
    return plan.surroundingDeleteDelayMs;
  }
  const std::string_view fe(frontendName);
  if (fe.starts_with("wayland")) {
    return plan.waylandSurroundingDeleteDelayMs;
  }
  return plan.surroundingDeleteDelayMs;
}

} // namespace areca
