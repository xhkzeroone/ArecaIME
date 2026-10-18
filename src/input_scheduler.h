#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include <fcitx-utils/event.h>

#include "bamboo_engine_adapter.h"
#include "key_queue.h"
#include "rewrite_backend.h"

namespace areca {

struct SchedulerTiming {
  uint32_t backspaceDelayMs = 1;
  uint32_t uinputBackspaceAckTimeoutMs = 150;
  uint32_t afterBackspaceWaitMs = 10;
  uint32_t waylandAfterBackspaceWaitMs = 3;
  uint32_t ximAfterBackspaceWaitMs = 10;
  uint32_t fcitx4AfterBackspaceWaitMs = 10;
  uint32_t dbusAfterBackspaceWaitMs = 10;
  uint32_t ibusAfterBackspaceWaitMs = 10;

  uint32_t uinputShiftSelectDelayMs = 1;
  uint32_t uinputShiftSelectAckTimeoutMs = 150;
  uint32_t afterUinputShiftSelectWaitMs = 20;
  uint32_t waylandAfterUinputShiftSelectWaitMs = 20;
  uint32_t ximAfterUinputShiftSelectWaitMs = 20;
  uint32_t fcitx4AfterUinputShiftSelectWaitMs = 20;
  uint32_t dbusAfterUinputShiftSelectWaitMs = 20;
  uint32_t ibusAfterUinputShiftSelectWaitMs = 20;

  uint32_t surroundingWaitMs = 3;
  uint32_t surroundingProbeTimeoutMs = 80;
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
  uint32_t postCommitDelayMs = 20;
  uint64_t timerAccuracyUsec = 1;
};

struct RewriteBackendSelection {
  RewriteBackend *backend = nullptr;
  uint32_t additionalBackspaces = 0;
  // Bắt đầu: FullReplace xóa toàn bộ từ cũ và chèn trọn vẹn từ mới cho từ đầu tiên trong thanh địa chỉ Chromium
  bool fullReplace = false;
  // Kết thúc: FullReplace xóa toàn bộ từ cũ và chèn trọn vẹn từ mới cho từ đầu tiên trong thanh địa chỉ Chromium
  bool probeSurrounding = false;
};

class InputScheduler {
public:
  using EngineResolver =
      std::function<VietnameseEngine *(fcitx::InputContext &)>;
  using TimingProvider =
      std::function<SchedulerTiming(fcitx::InputContext &)>;
  using DebugProvider = std::function<bool()>;
  using RewriteBackendSelector = std::function<RewriteBackendSelection(
      fcitx::InputContext &, const BambooResult &)>;

  InputScheduler(fcitx::EventLoop &eventLoop, EngineResolver engineResolver,
                 TimingProvider timingProvider, DebugProvider debugProvider,
                 RewriteBackendSelector rewriteBackendSelector);

  void enqueue(fcitx::InputContext &inputContext, uint32_t codepoint,
               std::string utf8Text);
  // Cho editor web nhận phím đầu qua frontend thay vì commitString để mở chế độ
  // soạn thảo. Vẫn xử lý Bamboo một lần để giữ trạng thái ghép dấu cho phím sau.
  // false: chưa sửa engine/event, caller có thể enqueue theo luồng thông thường.
  // true: đã xử lý xong hoặc bắt đầu applyResult, caller không enqueue lần nữa.
  bool handleIdleKey(fcitx::KeyEvent &event, uint32_t codepoint,
                     const std::string &utf8Text);
  void enqueueBackspace(fcitx::InputContext &inputContext);
  void resetContext(fcitx::InputContext &inputContext);

  size_t queuedKeyCount() const { return queue_.size(); }
  bool rewritePending() const { return activeTransactionId_ != 0; }
  bool stalled() const { return stalled_; }
  bool shouldRejectReset() const;

private:
  void scheduleNext();
  void processNext();
  void applyResult(fcitx::InputContext &inputContext, VietnameseEngine &engine,
                   const BambooResult &result, const std::string &rawText);
  void finishKey();
  void finishKeyAfterDelay();
  void finishKeyAfterCommit();
  void rewriteDone(uint64_t transactionId, RewriteOutcome outcome);

  fcitx::EventLoop &eventLoop_;
  EngineResolver engineResolver_;
  TimingProvider timingProvider_;
  DebugProvider debugProvider_;
  RewriteBackendSelector rewriteBackendSelector_;
  KeyQueue queue_;
  std::unique_ptr<fcitx::EventSourceTime> postCommitTimer_;
  SchedulerTiming activeTiming_;
  uint64_t nextSequence_ = 1;
  uint64_t nextTransactionId_ = 1;
  uint64_t activeTransactionId_ = 0;
  uint64_t lastRewriteCompletionTimeUsec_ = 0;
  bool processing_ = false;
  bool stalled_ = false;
};

} // namespace areca
