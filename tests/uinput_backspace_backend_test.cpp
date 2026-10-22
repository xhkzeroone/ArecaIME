#include <cassert>
#include <iostream>

#include <fcitx-utils/event.h>

#include "uinput_backspace_backend.h"
#include "uinput_device.h"
#include "uinput_key_ack_tracker.h"

int main() {
  areca::UinputKeyAckTracker tracker;
  tracker.reset(2);
  assert(tracker.expectedPresses() == 3);
  for (uint32_t i = 0; i < 2; ++i) {
    assert(tracker.observePress() ==
           areca::UinputKeyAckTracker::PressAction::Forward);
    assert(!tracker.shouldFilterRelease());
  }
  assert(tracker.observePress() ==
         areca::UinputKeyAckTracker::PressAction::FilterAndAcknowledge);
  assert(tracker.shouldFilterRelease());

  using Action = areca::UinputKeyAckTracker::PressAction;
  // GNOME Text Editor: phím xóa quay lại với cùng time/code và cờ Repeat.
  tracker.reset(1);
  assert(tracker.observePress(4392176, 22, false) == Action::Forward);
  assert(tracker.observePress(4392176, 22, true) == Action::Forward);
  assert(tracker.pressesSeen() == 1);
  assert(!tracker.shouldFilterRelease(4392176, 22));
  assert(!tracker.shouldFilterRelease(4392176, 22));
  assert(tracker.releasesSeen() == 1);
  assert(tracker.observePress(4392177, 22, false) ==
         Action::FilterAndAcknowledge);
  assert(tracker.shouldFilterRelease(4392177, 22));
  // Phím xác nhận gửi lại không được xác nhận lần nữa. Replay đến muộn
  // của phím xóa vẫn giữ quyết định forward sau khi đã nhận phím xác nhận.
  assert(tracker.observePress(4392177, 22, true) == Action::Filter);
  assert(tracker.observePress(4392176, 22, true) == Action::Forward);
  assert(!tracker.shouldFilterRelease(4392176, 22));
  assert(tracker.shouldFilterRelease(4392177, 22));
  assert(tracker.pressesSeen() == 2);
  assert(tracker.releasesSeen() == 2);

  // Repeat có timestamp mới vẫn là phím mới; không bỏ mọi cờ Repeat.
  tracker.reset(1);
  assert(tracker.observePress(10, 22, false) == Action::Forward);
  assert(tracker.observePress(11, 22, true) == Action::FilterAndAcknowledge);
  assert(!tracker.shouldFilterRelease(10, 22));
  assert(tracker.shouldFilterRelease(11, 22));

  // Không nhận diện trùng khi frontend không cung cấp timestamp/keycode.
  tracker.reset(1);
  assert(tracker.observePress(0, 22, false) == Action::Forward);
  assert(tracker.observePress(0, 22, true) == Action::FilterAndAcknowledge);
  assert(!tracker.shouldFilterRelease(0, 22));
  assert(tracker.shouldFilterRelease(0, 22));
  tracker.clear();
  tracker.reset(1);
  assert(tracker.observePress(4392176, 22, true) == Action::Forward);
  assert(!tracker.shouldFilterRelease(4392176, 22));

  fcitx::EventLoop eventLoop;
  areca::UinputDevice device([]() { return false; });
  areca::AdaptiveWait adaptiveWait;
  areca::UinputBackspaceBackend backend(eventLoop, device, adaptiveWait,
                                        []() { return false; });

  assert(std::string(backend.name()) == "uinput-backspace");
  assert(!backend.hasPending());

  bool available = backend.isAvailable();
  std::cout << "UinputBackspaceBackend test passed, uinput available="
            << (available ? "true" : "false") << "\n";
  return 0;
}
