#include "virtual_keyboard_backspace_backend.h"

#include <utility>

#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <linux/input-event-codes.h>

namespace areca {

VirtualKeyboardBackspaceBackend::VirtualKeyboardBackspaceBackend(
    fcitx::EventLoop &eventLoop, VirtualKeyboardDevice &device,
    DebugProvider debugProvider)
    : eventLoop_(eventLoop), device_(device),
      debugProvider_(std::move(debugProvider)) {}

VirtualKeyboardBackspaceBackend::~VirtualKeyboardBackspaceBackend() {
  clearPending();
  clearEcho();
}

bool VirtualKeyboardBackspaceBackend::isAvailable() {
  return device_.isAvailable();
}

ApplyStatus
VirtualKeyboardBackspaceBackend::apply(fcitx::InputContext &inputContext,
                                       const RewritePlan &plan,
                                       RewriteDone onDone) {
  if (hasPending() || !plan.transactionId || !device_.isAvailable()) {
    return ApplyStatus::Failed;
  }
  const auto codepoints = utf8Codepoints(plan.commitText);
  if (!plan.commitText.empty() && codepoints.empty()) {
    return ApplyStatus::Failed;
  }

  transactionId_ = plan.transactionId;
  inputContext_ = inputContext.watch();
  onDone_ = std::move(onDone);
  deletesLeft_ = plan.backspaceCount;
  const auto bursts = splitVirtualKeyboardBursts(codepoints);
  bursts_.assign(bursts.begin(), bursts.end());

  clearEcho();
  echoContext_ = inputContext.watch();
  const auto echo =
      virtualKeyboardEchoSequence(plan.backspaceCount, plan.commitText);
  pendingEcho_.assign(echo.begin(), echo.end());
  for (const auto value : echo) {
    if (value != 0) {
      injectedCodepoints_.insert(value);
    }
  }
  echoDeadlineUsec_ = fcitx::now(CLOCK_MONOTONIC) +
                      uint64_t{SyncTimeoutMs} * 1000 + EchoGraceUsec;

  if (debugProvider_()) {
    FCITX_INFO() << "areca: virtual-keyboard-backspace start tx="
                 << transactionId_ << " backspaces=" << deletesLeft_
                 << " bursts=" << bursts_.size()
                 << " commit=" << plan.commitText;
  }

  if (!sendNextBurst()) {
    clearPending();
    clearEcho();
    return ApplyStatus::Failed;
  }
  return ApplyStatus::Pending;
}

bool VirtualKeyboardBackspaceBackend::sendNextBurst() {
  const auto burst = std::move(bursts_.front());
  bursts_.pop_front();
  bool sent = device_.setKeymap(buildVirtualKeyboardKeymap(burst));
  // Backspace đi cùng đợt đầu, trước mọi ký tự mới.
  for (; sent && deletesLeft_ > 0; --deletesLeft_) {
    sent = device_.sendKey(KEY_BACKSPACE, true) &&
           device_.sendKey(KEY_BACKSPACE, false);
  }
  for (size_t slot = 0; sent && slot < burst.size(); ++slot) {
    const uint32_t keycode = virtualKeyboardCarrierKeycode(slot);
    sent = device_.sendKey(keycode, true) && device_.sendKey(keycode, false);
  }
  const uint64_t transactionId = transactionId_;
  sent = sent && device_.sync([this, transactionId](bool ok) {
    onBurstSynced(transactionId, ok);
  });
  if (sent) {
    armSyncTimeout();
  }
  return sent;
}

void VirtualKeyboardBackspaceBackend::onBurstSynced(uint64_t transactionId,
                                                    bool ok) {
  if (transactionId == 0 || transactionId != transactionId_) {
    return;
  }
  syncTimeout_.reset();
  if (!ok) {
    FCITX_ERROR() << "areca: virtual-keyboard-backspace send failed tx="
                  << transactionId;
    complete(RewriteOutcome::Failed);
    return;
  }
  if (bursts_.empty()) {
    complete(RewriteOutcome::Succeeded);
    return;
  }
  if (!inputContext_.get()) {
    completeWithoutSending();
    return;
  }
  if (!sendNextBurst()) {
    FCITX_ERROR() << "areca: virtual-keyboard-backspace send failed tx="
                  << transactionId;
    complete(RewriteOutcome::Failed);
  }
}

void VirtualKeyboardBackspaceBackend::armSyncTimeout() {
  const uint64_t transactionId = transactionId_;
  syncTimeout_ = eventLoop_.addTimeEvent(
      CLOCK_MONOTONIC,
      fcitx::now(CLOCK_MONOTONIC) + uint64_t{SyncTimeoutMs} * 1000, 0,
      [this, transactionId](fcitx::EventSourceTime *, uint64_t) {
        auto timer = std::move(syncTimeout_);
        if (transactionId == transactionId_) {
          FCITX_ERROR() << "areca: virtual-keyboard-backspace timed out tx="
                        << transactionId;
          complete(RewriteOutcome::Failed);
        }
        return false;
      });
  if (syncTimeout_) {
    syncTimeout_->setOneShot();
  }
}

void VirtualKeyboardBackspaceBackend::complete(RewriteOutcome outcome) {
  const uint64_t transactionId = transactionId_;
  auto onDone = std::move(onDone_);
  clearPending();
  if (outcome == RewriteOutcome::Succeeded) {
    // Phím đã phát xong nhưng có thể chưa dội về hết.
    echoDeadlineUsec_ = fcitx::now(CLOCK_MONOTONIC) + EchoGraceUsec;
  } else {
    clearEcho();
  }
  if (debugProvider_()) {
    FCITX_INFO() << "areca: virtual-keyboard-backspace done tx="
                 << transactionId
                 << " ok=" << (outcome == RewriteOutcome::Succeeded)
                 << " echo_pending=" << pendingEcho_.size();
  }
  if (onDone) {
    onDone(transactionId, outcome);
  }
}

void VirtualKeyboardBackspaceBackend::completeWithoutSending() {
  if (debugProvider_()) {
    FCITX_INFO() << "areca: virtual-keyboard-backspace context lost tx="
                 << transactionId_;
  }
  const uint64_t transactionId = transactionId_;
  auto onDone = std::move(onDone_);
  clearPending();
  clearEcho();
  if (onDone) {
    onDone(transactionId, RewriteOutcome::Succeeded);
  }
}

void VirtualKeyboardBackspaceBackend::clearPending() {
  syncTimeout_.reset();
  inputContext_.unwatch();
  onDone_ = {};
  transactionId_ = 0;
  deletesLeft_ = 0;
  bursts_.clear();
}

void VirtualKeyboardBackspaceBackend::clearEcho() {
  pendingEcho_.clear();
  injectedCodepoints_.clear();
  echoContext_.unwatch();
  echoDeadlineUsec_ = 0;
}

bool VirtualKeyboardBackspaceBackend::handleKeyEvent(fcitx::KeyEvent &event) {
  if (echoDeadlineUsec_ == 0) {
    return false;
  }
  if (fcitx::now(CLOCK_MONOTONIC) > echoDeadlineUsec_) {
    clearEcho();
    return false;
  }
  if (event.inputContext() != echoContext_.get()) {
    return false;
  }
  const auto sym = event.rawKey().sym();
  const bool backspace = sym == FcitxKey_BackSpace;
  const uint32_t codepoint = fcitx::Key::keySymToUnicode(sym);

  if (event.isRelease()) {
    // Nhả phím không làm đổi trạng thái; chỉ cần không để Bamboo thấy.
    if (backspace || (codepoint != 0 && injectedCodepoints_.count(codepoint))) {
      event.forward();
      return true;
    }
    return false;
  }

  if (pendingEcho_.empty()) {
    return false;
  }
  const uint32_t expected = pendingEcho_.front();
  const bool matches =
      expected == 0 ? backspace : (codepoint != 0 && codepoint == expected);
  if (!matches) {
    return false;
  }
  pendingEcho_.pop_front();
  event.forward();
  if (pendingEcho_.empty() && !hasPending()) {
    clearEcho();
  }
  return true;
}

} // namespace areca
