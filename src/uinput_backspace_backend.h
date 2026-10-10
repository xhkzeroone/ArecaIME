#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <fcitx-utils/event.h>
#include <fcitx-utils/trackableobject.h>

#include "adaptive_wait.h"
#include "event_loop_post.h"
#include "rewrite_backend.h"
#include "uinput_device.h"
#include "uinput_key_ack_tracker.h"

namespace areca {

class UinputBackspaceBackend final : public RewriteBackend {
public:
  using DebugProvider = std::function<bool()>;

  UinputBackspaceBackend(fcitx::EventLoop &eventLoop, UinputDevice &device,
                         AdaptiveWait &adaptiveWait,
                         DebugProvider debugProvider);
  ~UinputBackspaceBackend() override;

  const char *name() const override { return "uinput-backspace"; }
  ApplyStatus apply(fcitx::InputContext &inputContext, const RewritePlan &plan,
                    RewriteDone onDone) override;

  bool isAvailable();
  bool hasPending() const { return transactionId_ != 0; }
  // Xử lý Backspace uinput quay lại trong đúng transaction hiện tại.
  bool handleBackspace(fcitx::KeyEvent &event);

private:
  enum class TimerDispatch { TimerCallback, PostEvent };

  void sendNextBackspace();
  void scheduleNextBackspace();
  void scheduleCommit();
  void commitAfterAdaptiveWait(uint32_t appliedExtraWaitMs);
  void commitAndComplete();
  void completeWithoutCommit();
  void schedule(uint32_t delayMs, TimerDispatch dispatch,
                std::function<void()> callback);
  void dispatchPostEvent(uint64_t deadlineUsec,
                         std::function<void()> callback);
  void observeAndRun(uint64_t deadlineUsec,
                     std::function<void()> callback);
  void clearPending();

  fcitx::EventLoop &eventLoop_;
  UinputDevice &device_;
  EventLoopPostTask commitPost_;
  // State dùng chung với ForwardBackspaceBackend và probe lag toàn cục.
  AdaptiveWait &adaptiveWait_;
  DebugProvider debugProvider_;

  std::unique_ptr<fcitx::EventSourceTime> timer_;
  fcitx::TrackableObjectReference<fcitx::InputContext> inputContext_;
  RewriteDone onDone_;
  uint64_t transactionId_ = 0;
  uint32_t deletedCharacters_ = 0;
  uint32_t remainingBackspaces_ = 0;
  uint32_t sentBackspaces_ = 0;
  UinputKeyAckTracker backspaceTracker_;
  uint32_t backspaceDelayMs_ = 0;
  uint32_t afterBackspaceWaitMs_ = 0;
  uint64_t timerAccuracyUsec_ = 1;
  uint64_t ackSentAtUsec_ = 0;
  bool backspaceAckTimedOut_ = false;
  std::string commitText_;
};

} // namespace areca
