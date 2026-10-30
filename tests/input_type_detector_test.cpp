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
      tracker.focusClass_.clear();
    }
  }

  static void setClass(WindowFocusTracker &tracker, const std::string &className) {
    std::lock_guard<std::mutex> lock(tracker.focusProgramMutex_);
    tracker.focusClass_ = className;
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

  // Chromium variants can add prefixes or suffixes to the omnibox class.
  {
    fcitx::InputContextManager manager;
    fcitx::FocusGroup group("x11::0", manager);
    TestInputContext edge(manager, "microsoft-edge", "dbus");
    edge.setFocusGroup(&group);
    edge.setCapabilityFlags(fcitx::CapabilityFlag::Url);
    edge.setCursorRect(fcitx::Rect(100, 50, 101, 70));
    InputTypeDetector detector;
    // Url is authoritative and does not depend on AT-SPI availability.
    assert(detector.inChromiumAddressBar(edge));

    edge.setCapabilityFlags(fcitx::CapabilityFlags{});
    assert(!detector.inChromiumAddressBar(edge));

    WindowFocusTracker tracker;
    detector.setFocusTracker(&tracker);
    areca::WindowFocusTrackerTestAccess::seedFocus(
        tracker, ":1.53", "/omnibox", "msedge", false);
    areca::WindowFocusTrackerTestAccess::setClass(tracker, "OmniboxViewViews");
    assert(detector.inChromiumAddressBar(edge));
    // Neither coordinates nor the Url capability are needed for a match.
    edge.setCursorRect(fcitx::Rect(500, 600, 504, 640));
    edge.setCapabilityFlags(fcitx::CapabilityFlags{});
    assert(detector.inChromiumAddressBar(edge));

    // Page inputs must not match the current Omnibox substring rule.
    for (const auto *className : {"control   ", ""}) {
      areca::WindowFocusTrackerTestAccess::setClass(tracker, className);
      assert(!detector.inChromiumAddressBar(edge));
    }
    for (const auto *className : {"OmniboxResultView", "BraveOmniboxViewViews", "OmniboxViewViews extra",
                                 "CustomOmniboxViewViewsSubclass"}) {
      areca::WindowFocusTrackerTestAccess::setClass(tracker, className);
      assert(detector.inChromiumAddressBar(edge));
    }
    // Re-entering the page immediately clears the previous verdict.
    areca::WindowFocusTrackerTestAccess::setClass(tracker, "OmniboxViewViews");
    assert(detector.inChromiumAddressBar(edge));
    areca::WindowFocusTrackerTestAccess::seedFocus(
        tracker, ":1.53", "/page-input", "msedge", false);
    assert(!detector.inChromiumAddressBar(edge));

    areca::WindowFocusTrackerTestAccess::setClass(tracker, "OmniboxViewViews");
    // All browser identities use the existing Chromium list, without aliases.
    for (const auto *program : {"microsoft-edge-stable", "msedge",
                               "com.microsoft.Edge.desktop", "brave-browser",
                               "brave", "google-chrome-beta", "chromium-browser"}) {
      TestInputContext alias(manager, program);
      assert(detector.inChromiumAddressBar(alias));
    }
    TestInputContext chrome(manager, "google-chrome");
    assert(detector.inChromiumAddressBar(chrome));
    TestInputContext editor(manager, "gnome-text-editor");
    assert(!detector.inChromiumAddressBar(editor));
    areca::WindowFocusTrackerTestAccess::seedFocus(
        tracker, ":1.54", "/omnibox", "chrome", false);
    areca::WindowFocusTrackerTestAccess::setClass(tracker, "OmniboxViewViews");
    assert(detector.inChromiumAddressBar(chrome));
    assert(detector.inChromiumAddressBar(edge));

    areca::WindowFocusTrackerTestAccess::loseFocus(tracker, ":1.54", "/omnibox");
    assert(!detector.inChromiumAddressBar(chrome));
    assert(tracker.browserFocus().className.empty());
    areca::WindowFocusTrackerTestAccess::seedFocus(
        tracker, ":1.54", "/omnibox", "chrome", false);
    areca::WindowFocusTrackerTestAccess::setClass(tracker, "OmniboxViewViews");
    areca::WindowFocusTrackerTestAccess::connectionLost(tracker);
    assert(!detector.inChromiumAddressBar(chrome));
    assert(tracker.browserFocus().className.empty());
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

  // Case 5: canForwardBackspaces tests
  {
    fcitx::InputContextManager manager;
    InputTypeDetector detector(nullptr);

    // Wayland and XIM always forward
    {
      TestInputContext ic(manager, "app", "wayland");
      assert(detector.canForwardBackspaces(ic));
    }
    {
      TestInputContext ic(manager, "app", "wayland_v2");
      assert(detector.canForwardBackspaces(ic));
    }
    {
      TestInputContext ic(manager, "app", "xim");
      assert(detector.canForwardBackspaces(ic));
    }

    // SDL clients never forward backspaces
    {
      TestInputContext ic(manager, "SDL_Game", "dbus");
      ic.setCapabilityFlags(fcitx::CapabilityFlag::KeyEventOrderFix);
      assert(!detector.canForwardBackspaces(ic));
    }
    {
      TestInputContext ic(manager, "sdl2-game", "wayland");
      assert(!detector.canForwardBackspaces(ic));
    }
    {
      TestInputContext ic(manager, "/usr/bin/SDL_Game", "xim");
      assert(!detector.canForwardBackspaces(ic));
    }
    {
      TestInputContext ic(manager, "SDL3_App.desktop", "ibus");
      assert(!detector.canForwardBackspaces(ic));
    }

    // Frontend aliases use the same policy as their base frontend.
    {
      TestInputContext ic(manager, "app", "waylandfrontend");
      assert(detector.canForwardBackspaces(ic));
    }
    {
      TestInputContext ic(manager, "app", "dbusfrontend");
      ic.setCapabilityFlags(fcitx::CapabilityFlag::KeyEventOrderFix);
      assert(detector.canForwardBackspaces(ic));
    }
    {
      TestInputContext ic(manager, "app", "fcitx4frontend");
      ic.setCapabilityFlags(fcitx::CapabilityFlag::FormattedPreedit);
      assert(detector.canForwardBackspaces(ic));
    }

    // D-Bus without KeyEventOrderFix and without SurroundingText -> false
    {
      TestInputContext ic(manager, "game", "dbus");
      assert(!detector.canForwardBackspaces(ic));
    }

    // D-Bus with KeyEventOrderFix -> true
    {
      TestInputContext ic(manager, "app", "dbus");
      ic.setCapabilityFlags(fcitx::CapabilityFlag::KeyEventOrderFix);
      assert(detector.canForwardBackspaces(ic));
    }

    // D-Bus with SurroundingText -> true
    {
      TestInputContext ic(manager, "app", "dbus");
      ic.setCapabilityFlags(fcitx::CapabilityFlag::SurroundingText);
      assert(detector.canForwardBackspaces(ic));
    }
  }

  return 0;
}
