#include <cassert>
#include <iostream>

#include <fcitx-utils/event.h>

#include "uinput_shift_select_backend.h"

int main() {
  fcitx::EventLoop eventLoop;
  areca::UinputShiftSelectBackend backend(eventLoop, []() { return false; });

  assert(std::string(backend.name()) == "uinput-shift-select");
  assert(!backend.hasPending());

  bool available = backend.isAvailable();
  std::cout << "UinputShiftSelectBackend test passed, uinput available="
            << (available ? "true" : "false") << "\n";
  return 0;
}
