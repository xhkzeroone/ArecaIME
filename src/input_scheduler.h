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

using SchedulerTiming = RewriteTiming;

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
  // `forwardIfNoComposition`: tới lượt mà từ đang gõ đã trống thì chuyển
  // Backspace cho ứng dụng (sau lần sửa trước nên đúng thứ tự).
  void enqueueBackspace(fcitx::InputContext &inputContext,
                        bool forwardIfNoComposition = false);
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
