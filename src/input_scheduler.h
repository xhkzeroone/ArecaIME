#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include <fcitx-utils/event.h>

#include "bamboo_engine_adapter.h"
#include "key_queue.h"
#include "rewrite_backend.h"

namespace areca {

struct SchedulerTiming {
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
  // Chờ marker tới ứng dụng trước khi backend kênh phím bắt đầu xoá.
  uint32_t afterMarkerCommitWaitMs = 10;
  uint32_t waylandAfterMarkerCommitWaitMs = 10;
  uint32_t ximAfterMarkerCommitWaitMs = 10;
  uint32_t fcitx4AfterMarkerCommitWaitMs = 10;
  uint32_t dbusAfterMarkerCommitWaitMs = 10;
  uint32_t ibusAfterMarkerCommitWaitMs = 10;
  uint32_t postCommitDelayMs = 20;
  uint64_t timerAccuracyUsec = 1;
};

inline uint32_t resolveAfterMarkerCommitWaitMs(const char *frontendName,
                                               const SchedulerTiming &timing) {
  const std::string_view fe(frontendName ? frontendName : "");
  if (fe.starts_with("wayland")) {
    return timing.waylandAfterMarkerCommitWaitMs;
  }
  if (fe.starts_with("xim")) {
    return timing.ximAfterMarkerCommitWaitMs;
  }
  if (fe.starts_with("fcitx4")) {
    return timing.fcitx4AfterMarkerCommitWaitMs;
  }
  if (fe.starts_with("dbus")) {
    return timing.dbusAfterMarkerCommitWaitMs;
  }
  if (fe.starts_with("ibus")) {
    return timing.ibusAfterMarkerCommitWaitMs;
  }
  return timing.afterMarkerCommitWaitMs;
}

struct RewriteBackendSelection {
  RewriteBackend *backend = nullptr;
  bool probeSurrounding = false;
  // 0 giữ timing chung. Probe tự động đặt 150 ms cho lần đầu và 80 ms cho lần
  // retry ở từ kế tiếp.
  uint32_t surroundingProbeTimeoutMs = 0;
};

class InputScheduler {
public:
  using EngineResolver =
      std::function<VietnameseEngine *(fcitx::InputContext &)>;
  using TimingProvider = std::function<SchedulerTiming(fcitx::InputContext &)>;
  using DebugProvider = std::function<bool()>;
  using RewriteBackendSelector = std::function<RewriteBackendSelection(
      fcitx::InputContext &, const BambooResult &)>;
  // Báo mỗi lần text trong ứng dụng chắc chắn đã đổi do Areca: xoá
  // `deleteCount` ký tự trước con trỏ rồi chèn `text`.
  using CommitObserver = std::function<void(
      fcitx::InputContext &, uint32_t deleteCount, const std::string &text)>;

  InputScheduler(fcitx::EventLoop &eventLoop, EngineResolver engineResolver,
                 TimingProvider timingProvider, DebugProvider debugProvider,
                 RewriteBackendSelector rewriteBackendSelector);

  void enqueue(fcitx::InputContext &inputContext, uint32_t codepoint,
               std::string utf8Text, bool suppressMacro = false);
  // Cho editor web nhận phím đầu qua frontend thay vì commitString để mở chế độ
  // soạn thảo. Vẫn xử lý Bamboo một lần để giữ trạng thái ghép dấu cho phím
  // sau. false: chưa sửa engine/event, caller có thể enqueue theo luồng thông
  // thường. true: đã xử lý xong hoặc bắt đầu applyResult, caller không enqueue
  // lần nữa.
  bool handleIdleKey(fcitx::KeyEvent &event, uint32_t codepoint,
                     const std::string &utf8Text);
  void enqueueBackspace(fcitx::InputContext &inputContext);
  using ReplacedTextResolver =
      std::function<std::string(fcitx::InputContext &)>;
  // Xoá đoạn text `replaced` trả về lúc tới lượt rồi chèn `text`, qua cùng
  // rewrite backend như phím thường. Trả về rỗng thì bỏ qua.
  void enqueueReplacement(fcitx::InputContext &inputContext, std::string text,
                          ReplacedTextResolver replaced);
  void resetContext(fcitx::InputContext &inputContext);
  // Frontend báo surrounding mới: rewrite đang đợi marker vào ô thì bắt đầu
  // xoá.
  void notifySurroundingTextUpdated(fcitx::InputContext &inputContext);
  void setCommitObserver(CommitObserver observer) {
    commitObserver_ = std::move(observer);
  }

  size_t queuedKeyCount() const { return queue_.size(); }
  bool rewritePending() const { return activeTransactionId_ != 0; }
  bool stalled() const { return stalled_; }
  bool shouldRejectReset() const;

private:
  // Trình duyệt thường gửi reset vài chục ms sau khi chữ trong ô đổi. Reset đó
  // lọt qua sẽ xoá từ đang gõ dở và bắt restore đọc lại snapshot có thể còn
  // trễ, gây lặp chữ.
  static constexpr uint64_t kResetRejectWindowUsec = 50 * 1000;

  void scheduleNext();
  void processNext();
  void applyResult(fcitx::InputContext &inputContext, VietnameseEngine &engine,
                   const BambooResult &result, const std::string &rawText);
  void finishKey();
  void finishKeyAfterDelay();
  void finishKeyAfterCommit();
  void commitMarkerThenApply(fcitx::InputContext &inputContext,
                             RewriteBackend &backend, RewritePlan plan);
  void startRewrite(fcitx::InputContext &inputContext, RewriteBackend &backend,
                    const RewritePlan &plan);
  void proceedAfterMarker(const char *reason);
  void rewriteDone(uint64_t transactionId, RewriteOutcome outcome);
  void notifyPendingCommit();

  fcitx::EventLoop &eventLoop_;
  EngineResolver engineResolver_;
  TimingProvider timingProvider_;
  DebugProvider debugProvider_;
  RewriteBackendSelector rewriteBackendSelector_;
  CommitObserver commitObserver_;
  // Kết quả của rewrite đang chờ backend báo xong, để báo observer khi thành
  // công.
  fcitx::TrackableObjectReference<fcitx::InputContext> pendingCommitContext_;
  uint32_t pendingCommitDeleteCount_ = 0;
  std::string pendingCommitText_;
  KeyQueue queue_;
  std::unique_ptr<fcitx::EventSourceTime> postCommitTimer_;
  std::unique_ptr<fcitx::EventSourceTime> markerTimer_;
  // Rewrite đã commit marker, đang đợi nó tới ứng dụng rồi mới xoá.
  fcitx::TrackableObjectReference<fcitx::InputContext> markerContext_;
  RewriteBackend *markerBackend_ = nullptr;
  RewritePlan markerPlan_;
  bool markerAwaitsEcho_ = false;
  SchedulerTiming activeTiming_;
  uint64_t nextSequence_ = 1;
  uint64_t nextTransactionId_ = 1;
  uint64_t activeTransactionId_ = 0;
  uint64_t lastRewriteCompletionTimeUsec_ = 0;
  bool processing_ = false;
  bool stalled_ = false;
};

} // namespace areca
