#include <cassert>
#include <iostream>

#include <fcitx-utils/event.h>

#include "uinput_backspace_backend.h"
#include "uinput_device.h"

int main() {
  fcitx::EventLoop eventLoop;
  areca::UinputDevice device([]() { return false; });
  areca::UinputBackspaceBackend backend(eventLoop, device, []() { return false; });

  assert(std::string(backend.name()) == "uinput-backspace");
  assert(!backend.hasPending());

  bool available = backend.isAvailable();
  std::cout << "UinputBackspaceBackend test passed, uinput available="
            << (available ? "true" : "false") << "\n";
  return 0;
}
