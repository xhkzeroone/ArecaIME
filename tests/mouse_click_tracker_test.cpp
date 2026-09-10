#include "mouse_click_tracker.h"

#include <cassert>
#include <cerrno>
#include <sys/wait.h>
#include <unistd.h>

int main() {
  fcitx::EventLoop loop;
  areca::MouseClickTracker tracker(loop);
  for (int run = 0; run < 3; ++run) {
    assert(tracker.start());
    assert(tracker.start());
    for (int i = 0; i < 1000 && !tracker.hasPendingClick(); ++i) usleep(1000);
    assert(tracker.isValid());
    assert(tracker.hasPendingClick());
    // Reading again must preserve a reset deferred by the scheduler.
    assert(tracker.hasPendingClick());
    tracker.clearPendingClick();
    assert(!tracker.hasPendingClick());
    tracker.stop();
    assert(!tracker.isValid());
    assert(!tracker.hasPendingClick());
    assert(waitpid(-1, nullptr, WNOHANG) == -1 && errno == ECHILD);
  }
}
