#include "virtual_keyboard_backspace_backend.h"

#include <algorithm>
#include <utility>

#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <linux/input-event-codes.h>

namespace areca {

VirtualKeyboardBackspaceBackend::VirtualKeyboardBackspaceBackend(
    fcitx::EventLoop &eventLoop, VirtualKeyboardDevice &device,
    DebugProvider debugProvider, ModifierStateProvider modifierStateProvider)
    : eventLoop_(eventLoop), device_(device),
      debugProvider_(std::move(debugProvider)),
      modifierStateProvider_(std::move(modifierStateProvider)) {}

namespace {

constexpr uint32_t kLockedMask = static_cast<uint32_t>(
    fcitx::KeyStates{fcitx::KeyState::CapsLock, fcitx::KeyState::NumLock});
constexpr uint32_t kHeldMask = static_cast<uint32_t>(
    fcitx::KeyStates{fcitx::KeyState::Shift, fcitx::KeyState::Ctrl,
                     fcitx::KeyState::Alt, fcitx::KeyState::Super});

} // namespace

uint32_t VirtualKeyboardBackspaceBackend::modifierState() const {
  return modifierStateProvider_ ? modifierStateProvider_() : 0;
}

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
  device_.setLockedModifiers(modifierState() & kLockedMask);
  const auto bursts = splitVirtualKeyboardBursts(codepoints);
  bursts_.assign(bursts.begin(), bursts.end());
  const std::string_view marker = plan.typeMarker ? kRewriteMarker : "";
  if (plan.typeMarker) {
    // Marker đi riêng một đợt đầu, gõ trước các Backspace của đợt đó.
    bursts_.push_front(utf8Codepoints(marker));
    markerBurstPending_ = true;
  }

  clearEcho();
  echoContext_ = inputContext.watch();
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
  const auto sendBackspaces = [&] {
    for (; sent && deletesLeft_ > 0; --deletesLeft_) {
      sent = tapKey(KEY_BACKSPACE);
    }
  };
  const auto sendCarriers = [&] {
    for (size_t slot = 0; sent && slot < burst.size(); ++slot) {
      const uint32_t keycode = virtualKeyboardCarrierKeycode(slot);
      sent = tapKey(keycode);
    }
  };
  // Đợt marker: gõ marker rồi xoá. Đợt thường: Backspace còn lại đi trước mọi
  // ký tự mới.
  if (std::exchange(markerBurstPending_, false)) {
    sendCarriers();
    sendBackspaces();
  } else {
    sendBackspaces();
    sendCarriers();
  }
  // Trong lúc gõ, phím bổ trợ được nhả để Backspace không thành Alt+Backspace
  // hay Ctrl+Backspace. Gõ xong thì trả lại đúng phím người dùng đang giữ.
  if (sent && bursts_.empty()) {
    sent = device_.sendHeldModifiers(modifierState() & kHeldMask);
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

bool VirtualKeyboardBackspaceBackend::tapKey(uint32_t evdevKeycode) {
  // Keycode XKB = mã evdev + 8, đúng mã fcitx5 nhận lại.
  for (const bool press : {true, false}) {
    const uint32_t timeMs = nextKeyTimeMs_++;
    if (!device_.sendKey(evdevKeycode, press, timeMs)) {
      return false;
    }
    pendingEcho_.emplace_back(evdevKeycode + 8, timeMs);
  }
  return true;
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
  markerBurstPending_ = false;
  bursts_.clear();
}

void VirtualKeyboardBackspaceBackend::clearEcho() {
  pendingEcho_.clear();
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
  const auto echo =
      std::find(pendingEcho_.begin(), pendingEcho_.end(),
                std::make_pair(static_cast<uint32_t>(event.rawKey().code()),
                               static_cast<uint32_t>(event.time())));
  if (echo == pendingEcho_.end()) {
    return false;
  }
  pendingEcho_.erase(echo);
  event.forward();
  if (pendingEcho_.empty() && !hasPending()) {
    clearEcho();
  }
  return true;
}

} // namespace areca
