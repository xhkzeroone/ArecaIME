#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <fcitx-utils/event.h>
#include <fcitx-utils/trackableobject.h>

#include "adaptive_wait.h"
#include "event_loop_post.h"
#include "rewrite_backend.h"
#include "uinput_device.h"
#include "uinput_key_ack_tracker.h"

namespace areca {

class UinputShiftSelectBackend final : public RewriteBackend {
public:
  using DebugProvider = std::function<bool()>;

  UinputShiftSelectBackend(fcitx::EventLoop &eventLoop, UinputDevice &device,
                           AdaptiveWait &adaptiveWait,
                           DebugProvider debugProvider);
  UinputShiftSelectBackend(fcitx::EventLoop &eventLoop, UinputDevice &device,
                           DebugProvider debugProvider);
  ~UinputShiftSelectBackend() override;

  const char *name() const override { return "uinput-shift-select"; }
  ApplyStatus apply(fcitx::InputContext &inputContext, const RewritePlan &plan,
                    RewriteDone onDone) override;

  bool isAvailable();
  bool hasPending() const { return transactionId_ != 0; }
  bool handleSelectionLeft(fcitx::KeyEvent &event);

private:
  enum class TimerDispatch { TimerCallback, PostEvent };

  void beginSelection();
  void sendNextSelectionLeft();
  void releaseShiftThenCommit();
  void releaseShift();
  void commitSelectionAndComplete();
  void scheduleCommit();
  void completeWithoutCommit();
  void finishTransaction();
  void schedule(uint32_t delayMs, TimerDispatch dispatch,
                std::function<void()> callback);
  void clearPending();

  fcitx::EventLoop &eventLoop_;
  UinputDevice &device_;
  EventLoopPostTask commitPost_;
  AdaptiveWait *adaptiveWait_ = nullptr;
  DebugProvider debugProvider_;

  std::unique_ptr<fcitx::EventSourceTime> timer_;
  fcitx::TrackableObjectReference<fcitx::InputContext> inputContext_;
  RewriteDone onDone_;
  uint64_t transactionId_ = 0;
  uint32_t selectionCount_ = 0;
  uint32_t selectedCharacters_ = 0;
  UinputKeyAckTracker leftTracker_;
  uint32_t shiftSelectDelayMs_ = 0;
  uint32_t afterSelectWaitMs_ = 0;
  uint64_t timerAccuracyUsec_ = 1;
  uint64_t ackSentAtUsec_ = 0;
  bool shiftHeld_ = false;
  bool leftAckTimedOut_ = false;
  std::string commitText_;
};

} // namespace areca
