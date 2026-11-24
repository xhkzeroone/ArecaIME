#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace areca {

struct BambooResult {
  std::string currentText;
  std::string newText;
  uint32_t deleteCount = 0;
  std::string commitText;
  bool macroExpanded = false;
  bool spellcheckRestored = false;
};

// Commit trước mỗi lần xoá để ô nhập bỏ gợi ý tự hoàn thành hoặc vùng chọn
// đang nằm sau con trỏ: không gợi ý nào bắt đầu bằng ký tự này, nên sau đó mọi
// backend xoá như ở ô bình thường. Vì vậy lệnh xoá luôn xoá thêm một ký tự.
inline constexpr std::string_view kRewriteMarker = "\u00b7";

struct RewritePlan {
  uint64_t transactionId = 0;
  uint32_t backspaceCount = 0;
  uint32_t backspaceDelayMs = 1;
  uint32_t uinputBackspaceAckTimeoutMs = 150;
  uint32_t afterBackspaceWaitMs = 10;
  uint32_t waylandAfterBackspaceWaitMs = 10;
  uint32_t ximAfterBackspaceWaitMs = 10;
  uint32_t fcitx4AfterBackspaceWaitMs = 10;
  uint32_t dbusAfterBackspaceWaitMs = 10;
  uint32_t ibusAfterBackspaceWaitMs = 10;

  uint32_t uinputShiftSelectDelayMs = 1;
  uint32_t uinputShiftSelectAckTimeoutMs = 150;
  uint32_t afterUinputShiftSelectWaitMs = 5;
  uint32_t waylandAfterUinputShiftSelectWaitMs = 5;
  uint32_t ximAfterUinputShiftSelectWaitMs = 5;
  uint32_t fcitx4AfterUinputShiftSelectWaitMs = 5;
  uint32_t dbusAfterUinputShiftSelectWaitMs = 5;
  uint32_t ibusAfterUinputShiftSelectWaitMs = 5;

  uint32_t surroundingWaitMs = 3;
  uint32_t surroundingProbeTimeoutMs = 80;
  uint32_t nativeBackspaceDelayMs = 1;
  uint32_t afterNativeBackspaceWaitMs = 10;
  uint32_t waylandAfterNativeBackspaceWaitMs = 10;
  uint32_t ximAfterNativeBackspaceWaitMs = 10;
  uint32_t fcitx4AfterNativeBackspaceWaitMs = 10;
  uint32_t dbusAfterNativeBackspaceWaitMs = 10;
  uint32_t ibusAfterNativeBackspaceWaitMs = 10;

  uint64_t timerAccuracyUsec = 1;
  std::string commitText;
  bool probeSurrounding = false;
  // Backend phải tự gõ marker trước khi xoá vì scheduler không commit được
  // marker trên cùng kênh với nó.
  bool typeMarker = false;
};

template <typename Timing>
inline uint32_t resolveAfterNativeBackspaceWaitMs(const char *frontendName,
                                                  const Timing &plan) {
  if (!frontendName) {
    return plan.afterNativeBackspaceWaitMs;
  }
  const std::string_view fe(frontendName);
  if (fe.starts_with("wayland")) {
    return plan.waylandAfterNativeBackspaceWaitMs;
  }
  if (fe.starts_with("xim")) {
    return plan.ximAfterNativeBackspaceWaitMs;
  }
  if (fe.starts_with("fcitx4")) {
    return plan.fcitx4AfterNativeBackspaceWaitMs;
  }
  if (fe.starts_with("dbus")) {
    return plan.dbusAfterNativeBackspaceWaitMs;
  }
  if (fe.starts_with("ibus")) {
    return plan.ibusAfterNativeBackspaceWaitMs;
  }
  return plan.afterNativeBackspaceWaitMs;
}

template <typename Timing>
inline uint32_t resolveAfterBackspaceWaitMs(const char *frontendName,
                                            const Timing &plan) {
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

template <typename Timing>
inline uint32_t resolveAfterUinputShiftSelectWaitMs(const char *frontendName,
                                                    const Timing &plan) {
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

} // namespace areca
