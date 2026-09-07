#include <cassert>
#include <string>

#include "input_type_detector.h"
#include "window_focus_tracker.h"

int main() {
  using areca::InputTypeDetector;
  using areca::WindowFocusTracker;

  // Case 1: Null tracker
  {
    InputTypeDetector detector(nullptr);

    // Terminal by program name
    assert(detector.isTerminal("ghostty", nullptr));
    assert(detector.isTerminal("com.mitchellh.ghostty", "wayland"));
    assert(detector.isTerminal("mate-terminal", "xim"));

    // Not a terminal program
    assert(!detector.isTerminal("firefox", nullptr));
    assert(!detector.isTerminal("code", "wayland"));
    assert(!detector.isTerminal("", nullptr));
    assert(!detector.isTerminal("", "wayland"));

    // Nameless dbus frontend fallback
    assert(detector.isTerminal("", "dbus"));
    assert(detector.isTerminal("", "dbusfrontend"));
    assert(!detector.isTerminal("firefox", "dbus"));

    // Browser checks with null tracker
    assert(detector.isBrowser("google-chrome"));
    assert(detector.isBrowser("firefox"));
    assert(detector.isBrowser("thorium"));
    assert(detector.isBrowser("ladybird"));
    assert(!detector.isBrowser("code"));
    assert(!detector.isBrowser("kate"));
    assert(!detector.isBrowser("postman"));
    assert(!detector.isBrowser("redisinsight"));
    assert(!detector.isBrowser("slack"));
    assert(detector.isBrowser(""));
    assert(!detector.isBrowserUI());
    assert(!detector.isWebContent());
  }

  // Case 2: Inactive/invalid tracker (SPI disabled)
  {
    WindowFocusTracker tracker;
    assert(!tracker.isValid());
    InputTypeDetector detector(&tracker);

    // Browser checks with inactive tracker
    assert(detector.isBrowser("google-chrome"));
    assert(detector.isBrowser("firefox"));
    assert(!detector.isBrowser("code"));
    assert(!detector.isBrowser("postman"));
    assert(detector.isBrowser(""));
    assert(!detector.isBrowserUI());
    assert(!detector.isWebContent());

    // Terminal checks with inactive tracker
    assert(!detector.isTerminal("firefox", nullptr));
    assert(!detector.isTerminal("code", nullptr));
    assert(detector.isTerminal("ghostty", nullptr));
    assert(detector.isTerminal("gnome-terminal-server", nullptr));
    assert(detector.isTerminal("", "dbus"));
    assert(!detector.isTerminal("", "wayland"));
  }

  // Case 3: Setter wiring
  {
    InputTypeDetector detector;
    WindowFocusTracker tracker;

    detector.setFocusTracker(&tracker);
    assert(!detector.isBrowserUI());

    detector.setFocusTracker(nullptr);
    assert(!detector.isBrowserUI());
  }

  return 0;
}
