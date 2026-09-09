#include <cassert>
#include <string>

#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/rect.h>
#include <fcitx/focusgroup.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>

#include "input_type_detector.h"
#include "window_focus_tracker.h"

namespace areca {

struct WindowFocusTrackerTestAccess {
  static void seedFocus(WindowFocusTracker &tracker, const std::string &bus,
                        const std::string &path, const std::string &program,
                        bool terminal) {
    tracker.valid_.store(true);
    tracker.focusBus_ = bus;
    tracker.focusPath_ = path;
    tracker.focusInTerminal_.store(terminal);
    {
      std::lock_guard<std::mutex> lock(tracker.focusProgramMutex_);
      tracker.focusProgram_ = program;
    }
  }

  static void loseFocus(WindowFocusTracker &tracker, const char *sender,
                        const char *path = nullptr) {
    tracker.loseFocus(sender, path);
  }

  static void connectionLost(WindowFocusTracker &tracker) {
    tracker.connectionLost();
  }

  static void connectionEstablished(WindowFocusTracker &tracker) {
    tracker.connectionEstablished();
  }
};

} // namespace areca

class TestInputContext : public fcitx::InputContext {
public:
  TestInputContext(fcitx::InputContextManager &manager,
                   const std::string &program,
                   const char *frontendName = "x11")
      : fcitx::InputContext(manager, program), frontend_(frontendName) {}
  ~TestInputContext() override { destroy(); }

  const char *frontend() const override { return frontend_; }
  void setFrontend(const char *frontendName) { frontend_ = frontendName; }

  void commitStringImpl(const std::string &) override {}
  void deleteSurroundingTextImpl(int, unsigned int) override {}
  void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override {}
  void updatePreeditImpl() override {}

private:
  const char *frontend_ = "x11";
};

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

    // Chromium browser checks with null tracker
    assert(detector.isChromiumBrowser("google-chrome"));
    assert(detector.isChromiumBrowser("chromium"));
    assert(detector.isChromiumBrowser("brave"));
    assert(detector.isChromiumBrowser("microsoft-edge"));
    assert(!detector.isChromiumBrowser("firefox"));
    assert(!detector.isChromiumBrowser("code"));
    assert(!detector.isChromiumBrowser(""));
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

    detector.setFocusTracker(nullptr);
  }

  // Case 4: Chromium address bar detection
  {
    InputTypeDetector detector(nullptr);
    fcitx::InputContextManager manager;
    fcitx::FocusGroup x11Group("x11::0", manager);
    fcitx::FocusGroup waylandGroup("wayland:wayland-0", manager);

    // Method 1: Wayland with CapabilityFlag::Url
    {
      TestInputContext chromeWayland(manager, "google-chrome", "wayland");
      chromeWayland.setFocusGroup(&waylandGroup);
      chromeWayland.setCapabilityFlags(fcitx::CapabilityFlag::Url);
      assert(detector.inChromiumAddressBar(chromeWayland, "google-chrome"));

      // Non-url capability without Omnibox cursor rect
      chromeWayland.setCapabilityFlags(fcitx::CapabilityFlags{});
      assert(!detector.inChromiumAddressBar(chromeWayland, "google-chrome"));

      // Wayland also supports cursorRect fallback when compositor lacks Url flag
      chromeWayland.setCursorRect(fcitx::Rect(100, 50, 101, 70));
      assert(detector.inChromiumAddressBar(chromeWayland, "google-chrome"));
    }

    // Method 2: X11 Tier 2 cursorRect fallback
    {
      TestInputContext chromeX11(manager, "google-chrome", "x11");
      chromeX11.setFocusGroup(&x11Group);

      // Default rect 0x0
      assert(!detector.inChromiumAddressBar(chromeX11, "google-chrome"));

      // Valid Omnibox cursor rect: top < 200, width <= 2, 18 <= height <= 24
      chromeX11.setCursorRect(fcitx::Rect(100, 50, 101, 70));
      assert(detector.inChromiumAddressBar(chromeX11, "google-chrome"));

      // Valid boundary values
      chromeX11.setCursorRect(fcitx::Rect(0, 0, 2, 18));
      assert(detector.inChromiumAddressBar(chromeX11, "google-chrome"));
      chromeX11.setCursorRect(fcitx::Rect(0, 199, 2, 223));
      assert(detector.inChromiumAddressBar(chromeX11, "google-chrome"));

      // Invalid top >= 200
      chromeX11.setCursorRect(fcitx::Rect(100, 200, 101, 220));
      assert(!detector.inChromiumAddressBar(chromeX11, "google-chrome"));

      // Invalid negative top
      chromeX11.setCursorRect(fcitx::Rect(100, -1, 101, 19));
      assert(!detector.inChromiumAddressBar(chromeX11, "google-chrome"));

      // Invalid width > 2
      chromeX11.setCursorRect(fcitx::Rect(100, 50, 103, 70));
      assert(!detector.inChromiumAddressBar(chromeX11, "google-chrome"));

      // Invalid height < 18
      chromeX11.setCursorRect(fcitx::Rect(100, 50, 101, 67));
      assert(!detector.inChromiumAddressBar(chromeX11, "google-chrome"));

      // Invalid height > 24
      chromeX11.setCursorRect(fcitx::Rect(100, 50, 101, 75));
      assert(!detector.inChromiumAddressBar(chromeX11, "google-chrome"));

      // Non-Chromium browser on X11 does not match
      TestInputContext firefoxX11(manager, "firefox", "x11");
      firefoxX11.setFocusGroup(&x11Group);
      firefoxX11.setCursorRect(fcitx::Rect(100, 50, 101, 70));
      assert(!detector.inChromiumAddressBar(firefoxX11, "firefox"));
    }

    // Grace Window 5 seconds
    {
      TestInputContext chromeX11(manager, "google-chrome", "x11");
      chromeX11.setFocusGroup(&x11Group);
      chromeX11.setCursorRect(fcitx::Rect(100, 500, 101, 520));

      uint64_t verdictUsec = fcitx::now(CLOCK_MONOTONIC);
      assert(detector.inChromiumAddressBar(chromeX11, "google-chrome", &verdictUsec));

      // Expired grace window > 5s
      verdictUsec = fcitx::now(CLOCK_MONOTONIC) - 6000000;
      assert(!detector.inChromiumAddressBar(chromeX11, "google-chrome", &verdictUsec));
      assert(verdictUsec == 0);
    }

    // Test Address Bar First Word tracking logic
    {
      bool addrBarIsFirstWord = true;
      bool addrBarHadSpace = false;

      const auto computeAdditional = [&](bool inAddrBar) -> uint32_t {
        if (inAddrBar && addrBarIsFirstWord && !addrBarHadSpace) {
          addrBarIsFirstWord = false;
          return 1;
        }
        return 0;
      };

      // Initial first word replacement in address bar
      assert(computeAdditional(true) == 1);
      assert(!addrBarIsFirstWord);

      // Subsequent replacement within same first word
      assert(computeAdditional(true) == 0);

      // Space typed: next word is not first word
      addrBarIsFirstWord = false;
      addrBarHadSpace = true;
      assert(computeAdditional(true) == 0);

      // Backspacing when space had been typed does not re-arm first word
      if (!addrBarHadSpace) {
        addrBarIsFirstWord = true;
      }
      assert(!addrBarIsFirstWord);
      assert(computeAdditional(true) == 0);

      // Reset / Clear shortcut re-arms first word
      addrBarIsFirstWord = true;
      addrBarHadSpace = false;
      assert(computeAdditional(true) == 1);
      assert(!addrBarIsFirstWord);
    }
  }

  // Case 5: Tracker focus must not leak to another application.
  {
    WindowFocusTracker tracker;
    areca::WindowFocusTrackerTestAccess::seedFocus(
        tracker, ":1.20", "/terminal", "ghostty", true);
    InputTypeDetector detector(&tracker);

    assert(detector.isTerminal("", "wayland"));
    assert(!detector.isTerminal("firefox", "wayland"));

    // Different VS Code executable and desktop IDs still refer to the same
    // app whose embedded terminal owns the AT-SPI focus.
    areca::WindowFocusTrackerTestAccess::seedFocus(
        tracker, ":1.20", "/terminal", "code", true);
    assert(detector.isTerminal("visual-studio-code", "wayland"));

    // A delayed blur for the old node must not clear a newer focus.
    areca::WindowFocusTrackerTestAccess::seedFocus(
        tracker, ":1.21", "/new-entry", "firefox", false);
    areca::WindowFocusTrackerTestAccess::loseFocus(tracker, ":1.20",
                                                   "/terminal");
    assert(tracker.focusProgram() == "firefox");

    // Losing the active focus clears all public focus identity and flags.
    areca::WindowFocusTrackerTestAccess::loseFocus(tracker, ":1.21",
                                                   "/new-entry");
    assert(tracker.focusProgram().empty());
    assert(!tracker.isTerminalFocused());

    // A recovered AT-SPI connection makes the tracker usable again.
    areca::WindowFocusTrackerTestAccess::connectionLost(tracker);
    assert(!tracker.isValid());
    areca::WindowFocusTrackerTestAccess::connectionEstablished(tracker);
    assert(tracker.isValid());
  }

  return 0;
}
