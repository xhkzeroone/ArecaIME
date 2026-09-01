#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <fcitx-utils/event.h>
#include <fcitx-utils/trackableobject.h>

#include "rewrite_backend.h"

namespace areca {

class UinputBackspaceBackend final : public RewriteBackend {
public:
  using DebugProvider = std::function<bool()>;

  UinputBackspaceBackend(fcitx::EventLoop &eventLoop,
                         DebugProvider debugProvider);
  ~UinputBackspaceBackend() override;

  const char *name() const override { return "uinput-backspace"; }
  ApplyStatus apply(fcitx::InputContext &inputContext, const RewritePlan &plan,
                    RewriteDone onDone) override;

  bool isAvailable();
  bool hasPending() const { return transactionId_ != 0; }

private:
  bool ensureDevice();
  void closeDevice();
  void sendKeyEvent(uint16_t code, int value);
  void beginSelectionAndDelete();
  void sendNextSelectionLeft();
  void releaseShiftThenCommit();
  void releaseShift();
  void commitSelectionAndComplete();
  void commitNextChar(size_t index);
  void scheduleCommit();
  void commitAndComplete();
  void completeWithoutCommit();
  void schedule(uint32_t delayMs, std::function<void()> callback);
  void clearPending();

  fcitx::EventLoop &eventLoop_;
  DebugProvider debugProvider_;
  int uinputFd_ = -1;
  bool deviceInitialized_ = false;

  std::unique_ptr<fcitx::EventSourceTime> timer_;
  fcitx::TrackableObjectReference<fcitx::InputContext> inputContext_;
  RewriteDone onDone_;
  uint64_t transactionId_ = 0;
  uint32_t selectionCount_ = 0;
  uint32_t selectedCharacters_ = 0;
  uint32_t backspaceDelayMs_ = 0;
  uint32_t afterBackspaceWaitMs_ = 0;
  uint64_t timerAccuracyUsec_ = 1;
  bool shiftHeld_ = false;
  std::string commitText_;
  std::vector<std::string> commitChars_;
};

} // namespace areca
