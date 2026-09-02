#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <fcitx-utils/event.h>
#include <fcitx-utils/trackableobject.h>

#include "rewrite_backend.h"
#include "uinput_device.h"

namespace areca {

class UinputBackspaceBackend final : public RewriteBackend {
public:
  using DebugProvider = std::function<bool()>;

  UinputBackspaceBackend(fcitx::EventLoop &eventLoop, UinputDevice &device,
                         DebugProvider debugProvider);
  ~UinputBackspaceBackend() override;

  const char *name() const override { return "uinput-backspace"; }
  ApplyStatus apply(fcitx::InputContext &inputContext, const RewritePlan &plan,
                    RewriteDone onDone) override;

  bool isAvailable();
  bool hasPending() const { return transactionId_ != 0; }

private:
  void sendNextBackspace();
  void scheduleNextBackspace();
  void scheduleCommit();
  void commitAndComplete();
  void completeWithoutCommit();
  void schedule(uint32_t delayMs, std::function<void()> callback);
  void clearPending();

  fcitx::EventLoop &eventLoop_;
  UinputDevice &device_;
  DebugProvider debugProvider_;

  std::unique_ptr<fcitx::EventSourceTime> timer_;
  fcitx::TrackableObjectReference<fcitx::InputContext> inputContext_;
  RewriteDone onDone_;
  uint64_t transactionId_ = 0;
  uint32_t remainingBackspaces_ = 0;
  uint32_t sentBackspaces_ = 0;
  uint32_t backspaceDelayMs_ = 0;
  uint32_t afterBackspaceWaitMs_ = 0;
  uint64_t timerAccuracyUsec_ = 1;
  std::string commitText_;
};

} // namespace areca
