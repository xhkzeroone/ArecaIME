#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <fcitx-utils/event.h>
#include <fcitx-utils/trackableobject.h>

#include "event_loop_post.h"
#include "rewrite_backend.h"

namespace areca {

// Trước mỗi lần xoá, đọc lại snapshot theo nhịp cho tới khi text trước con trỏ
// đúng là từ Areca đã gõ ra, tức ứng dụng đã xử lý xong các lượt trước. Khớp
// thì xoá rồi commit liền; hết giờ thì chưa xoá gì nên trả NeedsFallback để
// scheduler chạy lại lượt đó bằng Backspace. Lượt đầu của ô thì xoá xong poll
// tiếp tới khi snapshot cho thấy chữ đã mất mới commit, vì có ứng dụng báo
// surrounding đúng nhưng lờ lệnh xoá. Không tự sửa snapshot sau khi xoá hay
// commit, để lượt sau chỉ tin snapshot ứng dụng gửi.
class SurroundingTextBackend final : public RewriteBackend {
public:
  using DebugProvider = std::function<bool()>;
  // Kết quả kiểm tra ứng dụng có làm theo lệnh xoá không.
  using DeleteCheckHandler =
      std::function<void(fcitx::InputContext &, bool confirmed)>;

  SurroundingTextBackend(fcitx::EventLoop &eventLoop,
                         DebugProvider debugProvider,
                         DeleteCheckHandler onDeleteChecked = nullptr);
  ~SurroundingTextBackend() override;

  SurroundingTextBackend(const SurroundingTextBackend &) = delete;
  SurroundingTextBackend &operator=(const SurroundingTextBackend &) = delete;

  const char *name() const override { return "surrounding-text"; }
  MarkerDelivery markerDelivery() const override {
    return MarkerDelivery::None;
  }
  ApplyStatus apply(fcitx::InputContext &inputContext, const RewritePlan &plan,
                    RewriteDone onDone) override;

  bool hasPending() const { return transactionId_ != 0; }

private:
  void pollSnapshot();
  void deleteAndCommit();
  void deleteAndVerify();
  void pollDelete();
  bool schedulePoll(uint64_t nowUsec, uint64_t deadlineUsec,
                    void (SurroundingTextBackend::*poll)());
  void commitAndFinish(fcitx::InputContext &inputContext);
  void finish(RewriteOutcome outcome);
  void clearPending();

  fcitx::EventLoop &eventLoop_;
  EventLoopPostTask commitPost_;
  DebugProvider debugProvider_;
  DeleteCheckHandler onDeleteChecked_;
  std::unique_ptr<fcitx::EventSourceTime> timer_;
  fcitx::TrackableObjectReference<fcitx::InputContext> inputContext_;
  RewriteDone onDone_;
  uint64_t transactionId_ = 0;
  uint32_t backspaceCount_ = 0;
  std::string commitText_;
  // Text trước con trỏ phải kết thúc bằng từ đang gõ mới được xoá.
  std::string expectedTail_;
  uint32_t pollMs_ = 1;
  uint64_t timerAccuracyUsec_ = 1;
  uint64_t checkStartUsec_ = 0;
  uint64_t checkDeadlineUsec_ = 0;
  uint32_t pollCount_ = 0;
  bool verifyDelete_ = false;
  uint32_t deleteCheckWaitMs_ = 0;
  std::string originalText_;
  unsigned int originalCursor_ = 0;
  std::string expectedText_;
  unsigned int expectedCursor_ = 0;
};

} // namespace areca
