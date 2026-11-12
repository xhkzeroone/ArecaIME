#include "virtual_keyboard_backend.h"

#include <utility>

#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>

namespace areca {
VirtualKeyboardBackend::VirtualKeyboardBackend(
    fcitx::EventLoop &eventLoop, std::unique_ptr<VirtualKeyboardDevice> device,
    MainThreadPoster postToMain, DebugProvider debugProvider)
    : eventLoop_(eventLoop), device_(std::move(device)),
      postToMain_(std::move(postToMain)),
      debugProvider_(std::move(debugProvider)) {}

VirtualKeyboardBackend::~VirtualKeyboardBackend() {
  *alive_ = false;
  if (device_) {
    device_->cancel();
  }
}

bool VirtualKeyboardBackend::isAvailable() {
  return device_ && device_->isAvailable();
}

ApplyStatus VirtualKeyboardBackend::apply(fcitx::InputContext &inputContext,
                                          const RewritePlan &plan,
                                          RewriteDone onDone) {
  if (hasPending() || !plan.transactionId) {
    return ApplyStatus::Failed;
  }
  if (!isAvailable()) {
    return ApplyStatus::Failed;
  }
  auto *device = device_.get();

  transactionId_ = plan.transactionId;
  onDone_ = std::move(onDone);
  echoContext_ = inputContext.watch();
  const auto echo =
      virtualKeyboardEchoSequence(plan.backspaceCount, plan.commitText);
  pendingEcho_.assign(echo.begin(), echo.end());
  injectedCodepoints_.clear();
  for (const auto value : echo) {
    if (value != 0) {
      injectedCodepoints_.insert(value);
    }
  }
  echoDeadlineUsec_ = fcitx::now(CLOCK_MONOTONIC) +
                      uint64_t{kWatchdogMs} * 1000 + kEchoGraceUsec;

  if (debugProvider_()) {
    FCITX_INFO() << "areca: virtual-keyboard start tx=" << transactionId_
                 << " device=" << device->kind()
                 << " backspaces=" << plan.backspaceCount
                 << " commit=" << plan.commitText;
  }

  const uint64_t transactionId = transactionId_;
  watchdog_ = eventLoop_.addTimeEvent(
      CLOCK_MONOTONIC,
      fcitx::now(CLOCK_MONOTONIC) + uint64_t{kWatchdogMs} * 1000, 0,
      [this, transactionId](fcitx::EventSourceTime *, uint64_t) {
        FCITX_ERROR() << "areca: virtual-keyboard timed out tx="
                      << transactionId;
        finish(transactionId, false);
        return true;
      });
  watchdog_->setOneShot();

  VirtualKeyboardRequest request;
  request.deleteCount = plan.backspaceCount;
  request.text = plan.commitText;
  std::weak_ptr<bool> alive = alive_;
  device->replace(request, [this, alive, transactionId](bool ok) {
    postToMain_([this, alive, transactionId, ok] {
      if (auto token = alive.lock(); token && *token) {
        finish(transactionId, ok);
      }
    });
  });
  return ApplyStatus::Pending;
}

void VirtualKeyboardBackend::finish(uint64_t transactionId, bool ok) {
  if (transactionId == 0 || transactionId != transactionId_) {
    return;
  }
  auto watchdog = std::move(watchdog_);
  auto onDone = std::move(onDone_);
  transactionId_ = 0;
  echoDeadlineUsec_ = fcitx::now(CLOCK_MONOTONIC) + kEchoGraceUsec;
  if (debugProvider_()) {
    FCITX_INFO() << "areca: virtual-keyboard done tx=" << transactionId
                 << " ok=" << ok << " echo_pending=" << pendingEcho_.size();
  }
  if (onDone) {
    onDone(transactionId,
           ok ? RewriteOutcome::Succeeded : RewriteOutcome::Failed);
  }
}

void VirtualKeyboardBackend::clearEcho() {
  pendingEcho_.clear();
  injectedCodepoints_.clear();
  echoContext_.unwatch();
  echoDeadlineUsec_ = 0;
}

bool VirtualKeyboardBackend::handleKeyEvent(fcitx::KeyEvent &event) {
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
