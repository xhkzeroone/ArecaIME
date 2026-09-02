#include "uinput_backspace_backend.h"

#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <string_view>
#include <sys/ioctl.h>
#include <unistd.h>
#include <utility>

#include <fcitx-utils/log.h>
#include <fcitx-utils/utf8.h>

namespace areca {

UinputBackspaceBackend::UinputBackspaceBackend(fcitx::EventLoop &eventLoop,
                                               DebugProvider debugProvider)
    : eventLoop_(eventLoop), debugProvider_(std::move(debugProvider)) {}

UinputBackspaceBackend::~UinputBackspaceBackend() {
  clearPending();
  closeDevice();
}

bool UinputBackspaceBackend::isAvailable() { return ensureDevice(); }

bool UinputBackspaceBackend::ensureDevice() {
  if (deviceInitialized_) {
    return uinputFd_ >= 0;
  }
  deviceInitialized_ = true;

  uinputFd_ = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (uinputFd_ < 0) {
    uinputFd_ = open("/dev/input/uinput", O_WRONLY | O_NONBLOCK);
  }
  if (uinputFd_ < 0) {
    if (debugProvider_()) {
      FCITX_INFO() << "areca: uinput failed to open /dev/uinput";
    }
    return false;
  }

  if (ioctl(uinputFd_, UI_SET_EVBIT, EV_KEY) < 0 ||
      ioctl(uinputFd_, UI_SET_KEYBIT, KEY_LEFTSHIFT) < 0 ||
      ioctl(uinputFd_, UI_SET_KEYBIT, KEY_LEFT) < 0 ||
      ioctl(uinputFd_, UI_SET_EVBIT, EV_SYN) < 0) {
    closeDevice();
    return false;
  }

#ifdef UI_DEV_SETUP
  struct uinput_setup usetup;
  std::memset(&usetup, 0, sizeof(usetup));
  usetup.id.bustype = BUS_USB;
  usetup.id.vendor = 0x1234;
  usetup.id.product = 0x5678;
  std::strncpy(usetup.name, "ArecaIME Virtual Keyboard",
               UINPUT_MAX_NAME_SIZE - 1);
  if (ioctl(uinputFd_, UI_DEV_SETUP, &usetup) < 0 ||
      ioctl(uinputFd_, UI_DEV_CREATE) < 0) {
#endif
    struct uinput_user_dev udev;
    std::memset(&udev, 0, sizeof(udev));
    std::strncpy(udev.name, "ArecaIME Virtual Keyboard",
                 UINPUT_MAX_NAME_SIZE - 1);
    udev.id.bustype = BUS_USB;
    udev.id.vendor = 0x1234;
    udev.id.product = 0x5678;
    if (write(uinputFd_, &udev, sizeof(udev)) < 0 ||
        ioctl(uinputFd_, UI_DEV_CREATE) < 0) {
      closeDevice();
      return false;
    }
#ifdef UI_DEV_SETUP
  }
#endif

  if (debugProvider_()) {
    FCITX_INFO() << "areca: uinput device initialized successfully (fd="
                 << uinputFd_ << ")";
  }
  return true;
}

void UinputBackspaceBackend::closeDevice() {
  if (uinputFd_ >= 0) {
    ioctl(uinputFd_, UI_DEV_DESTROY);
    close(uinputFd_);
    uinputFd_ = -1;
  }
}

void UinputBackspaceBackend::sendKeyEvent(uint16_t code, int value) {
  if (uinputFd_ < 0) {
    return;
  }

  struct input_event ev;
  std::memset(&ev, 0, sizeof(ev));
  ev.type = EV_KEY;
  ev.code = code;
  ev.value = value;
  (void)write(uinputFd_, &ev, sizeof(ev));

  std::memset(&ev, 0, sizeof(ev));
  ev.type = EV_SYN;
  ev.code = SYN_REPORT;
  ev.value = 0;
  (void)write(uinputFd_, &ev, sizeof(ev));
}

ApplyStatus UinputBackspaceBackend::apply(fcitx::InputContext &inputContext,
                                          const RewritePlan &plan,
                                          RewriteDone onDone) {
  if (hasPending() || !plan.transactionId || !ensureDevice()) {
    return ApplyStatus::Failed;
  }

  const uint64_t capMask = inputContext.capabilityFlags().toInteger();
  constexpr uint64_t kForwardBackspaceCapabilityMask = 0x72;
  splitCommitChars_ = (capMask == kForwardBackspaceCapabilityMask);

  transactionId_ = plan.transactionId;
  inputContext_ = inputContext.watch();
  onDone_ = std::move(onDone);
  selectionCount_ = plan.backspaceCount;
  selectedCharacters_ = plan.backspaceCount;
  backspaceDelayMs_ = plan.backspaceDelayMs;
  const char *frontend = inputContext.frontend();
  afterBackspaceWaitMs_ = resolveAfterBackspaceWaitMs(frontend, plan);
  timerAccuracyUsec_ = plan.timerAccuracyUsec;
  commitText_ = plan.commitText;

  if (debugProvider_()) {
    FCITX_INFO() << "areca: uinput-backspace start tx=" << transactionId_
                 << " cap_mask=0x" << std::hex << capMask << std::dec
                 << " split_commit=" << splitCommitChars_
                 << " select_left=" << selectionCount_
                 << " delay_ms=" << backspaceDelayMs_
                 << " after_wait_ms=" << afterBackspaceWaitMs_
                 << " frontend=" << (frontend ? frontend : "")
                 << " accuracy_us=" << timerAccuracyUsec_;
  }

  if (!selectionCount_) {
    scheduleCommit();
  } else {
    beginSelection();
  }
  return ApplyStatus::Pending;
}

void UinputBackspaceBackend::beginSelection() {
  auto *inputContext = inputContext_.get();
  if (!inputContext) {
    completeWithoutCommit();
    return;
  }

  sendKeyEvent(KEY_LEFTSHIFT, 1); // Shift down
  shiftHeld_ = true;
  // Wait one backspaceDelayMs cycle before the first Left so the browser
  // has time to flush the Shift modifier state (needed for React/Facebook).
  schedule(backspaceDelayMs_, [this]() { sendNextSelectionLeft(); });
}

void UinputBackspaceBackend::sendNextSelectionLeft() {
  if (!inputContext_.get()) {
    releaseShift();
    completeWithoutCommit();
    return;
  }

  sendKeyEvent(KEY_LEFT, 1); // Left press
  sendKeyEvent(KEY_LEFT, 0); // Left release
  --selectionCount_;

  if (selectionCount_) {
    schedule(backspaceDelayMs_, [this]() { sendNextSelectionLeft(); });
    return;
  }

  // Last Left done — delay before Shift UP.
  schedule(backspaceDelayMs_, [this]() { releaseShiftThenCommit(); });
}

void UinputBackspaceBackend::releaseShiftThenCommit() {
  releaseShift();
  // Delay after Shift UP before committing, giving the browser time to
  // finalize the selection before the IM protocol commit replaces it.
  schedule(backspaceDelayMs_, [this]() { commitSelectionAndComplete(); });
}

void UinputBackspaceBackend::releaseShift() {
  if (!shiftHeld_) {
    return;
  }
  sendKeyEvent(KEY_LEFTSHIFT, 0); // Shift up
  shiftHeld_ = false;
}

void UinputBackspaceBackend::commitSelectionAndComplete() {
  auto *inputContext = inputContext_.get();
  if (!inputContext) {
    completeWithoutCommit();
    return;
  }

  if (!splitCommitChars_) {
    if (debugProvider_()) {
      FCITX_INFO() << "areca: uinput-select batch commit tx=" << transactionId_
                   << " chars=" << selectedCharacters_
                   << " commit=" << commitText_;
    }
    if (!commitText_.empty()) {
      inputContext->commitString(commitText_);
    }
    const uint64_t transactionId = transactionId_;
    auto onDone = std::move(onDone_);
    clearPending();
    if (onDone) {
      onDone(transactionId);
    }
    return;
  }

  commitChars_.clear();
  auto it = commitText_.begin();
  while (it != commitText_.end()) {
    auto start = it;
    uint32_t codepoint = 0;
    it = fcitx::utf8::getNextChar(it, commitText_.end(), &codepoint);
    commitChars_.emplace_back(start, it);
  }

  if (debugProvider_()) {
    FCITX_INFO() << "areca: uinput-select split commit (10ms) tx=" << transactionId_
                 << " chars=" << selectedCharacters_
                 << " commit=" << commitText_
                 << " split_count=" << commitChars_.size();
  }

  if (commitChars_.empty()) {
    const uint64_t transactionId = transactionId_;
    auto onDone = std::move(onDone_);
    clearPending();
    if (onDone) {
      onDone(transactionId);
    }
  } else {
    commitNextChar(0);
  }
}

void UinputBackspaceBackend::commitNextChar(size_t index) {
  auto *inputContext = inputContext_.get();
  if (!inputContext) {
    completeWithoutCommit();
    return;
  }

  inputContext->commitString(commitChars_[index]);

  if (index + 1 < commitChars_.size()) {
    const uint32_t charDelayMs = std::max(10U, backspaceDelayMs_);
    schedule(charDelayMs, [this, index]() { commitNextChar(index + 1); });
  } else {
    const uint64_t transactionId = transactionId_;
    auto onDone = std::move(onDone_);
    clearPending();
    if (onDone) {
      onDone(transactionId);
    }
  }
}

void UinputBackspaceBackend::scheduleCommit() {
  schedule(afterBackspaceWaitMs_, [this]() { commitAndComplete(); });
}

void UinputBackspaceBackend::commitAndComplete() {
  auto *inputContext = inputContext_.get();
  if (!inputContext) {
    completeWithoutCommit();
    return;
  }

  if (!commitText_.empty()) {
    inputContext->commitString(commitText_);
  }

  const uint64_t transactionId = transactionId_;
  auto onDone = std::move(onDone_);
  if (debugProvider_()) {
    FCITX_INFO() << "areca: uinput-backspace complete tx=" << transactionId
                 << " commit=" << commitText_;
  }
  clearPending();
  if (onDone) {
    onDone(transactionId);
  }
}

void UinputBackspaceBackend::completeWithoutCommit() {
  const uint64_t transactionId = transactionId_;
  auto onDone = std::move(onDone_);
  if (debugProvider_()) {
    FCITX_INFO() << "areca: uinput-backspace context lost tx=" << transactionId;
  }
  clearPending();
  if (onDone) {
    onDone(transactionId);
  }
}

void UinputBackspaceBackend::schedule(uint32_t delayMs,
                                      std::function<void()> callback) {
  timer_.reset();
  const uint64_t deadline =
      fcitx::now(CLOCK_MONOTONIC) + static_cast<uint64_t>(delayMs) * 1000;
  timer_ =
      eventLoop_.addTimeEvent(CLOCK_MONOTONIC, deadline, timerAccuracyUsec_,
                              [this, callback = std::move(callback)](
                                  fcitx::EventSourceTime *, uint64_t) mutable {
                                auto completedTimer = std::move(timer_);
                                callback();
                                return false;
                              });
  if (!timer_) {
    callback();
    return;
  }
  timer_->setOneShot();
}

void UinputBackspaceBackend::clearPending() {
  timer_.reset();
  releaseShift();
  inputContext_.unwatch();
  onDone_ = {};
  transactionId_ = 0;
  selectionCount_ = 0;
  selectedCharacters_ = 0;
  backspaceDelayMs_ = 0;
  afterBackspaceWaitMs_ = 0;
  timerAccuracyUsec_ = 1;
  shiftHeld_ = false;
  splitCommitChars_ = false;
  commitText_.clear();
  commitChars_.clear();
}

} // namespace areca
