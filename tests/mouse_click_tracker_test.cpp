#include "mouse_click_tracker.h"

#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <functional>
#include <sys/wait.h>
#include <unistd.h>

namespace {

// WNOWAIT để chỉ quan sát: tiến trình con phải do chính tracker thu hồi.
bool noChildLeft() {
  siginfo_t info{};
  return waitid(P_ALL, 0, &info, WEXITED | WNOHANG | WNOWAIT) == -1 &&
         errno == ECHILD;
}

// Loop sd-event không chạy lại được sau exit(), nên mỗi kịch bản dùng một
// loop và chỉ exec một lần cho tới khi điều kiện đúng hoặc hết hạn.
bool runUntil(fcitx::EventLoop &loop, const std::function<bool()> &done) {
  bool satisfied = false;
  int ticks = 0;
  auto timer =
      loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC), 1000,
                        [&](fcitx::EventSourceTime *source, uint64_t) {
                          satisfied = done();
                          if (satisfied || ++ticks > 400) {
                            loop.exit();
                            return true;
                          }
                          source->setNextInterval(5000);
                          source->setOneShot();
                          return true;
                        });
  timer->setOneShot();
  loop.exec();
  return satisfied;
}

void waitClick(areca::MouseClickTracker &tracker) {
  for (int i = 0; i < 1000 && !tracker.hasPendingClick(); ++i)
    usleep(1000);
}

} // namespace

int main() {
  for (int run = 0; run < 3; ++run) {
    fcitx::EventLoop loop;
    areca::MouseClickTracker tracker(loop);
    assert(tracker.start());
    assert(tracker.start());
    waitClick(tracker);
    assert(tracker.isValid());
    assert(tracker.hasPendingClick());
    // Reading again must preserve a reset deferred by the scheduler.
    assert(tracker.hasPendingClick());
    tracker.clearPendingClick();
    assert(!tracker.hasPendingClick());
    tracker.stop();
    assert(!tracker.isValid());
    assert(!tracker.hasPendingClick());
    // stop() không chặn chờ helper; timer trên loop thu hồi nó sau.
    assert(runUntil(loop, noChildLeft));
  }
  // Tắt cấu hình hủy unique_ptr khi click vẫn pending: phải thu hồi helper,
  // không còn watcher hoặc callback truy cập tracker đã bị hủy.
  {
    fcitx::EventLoop loop;
    auto enabled = std::make_unique<areca::MouseClickTracker>(loop);
    assert(enabled->start());
    waitClick(*enabled);
    assert(enabled->hasPendingClick());
    enabled.reset();
    assert(runUntil(loop, noChildLeft));
  }
  // Helper thoát ngay: thu hồi luôn và không spawn lại ở lần activate kế tiếp.
  {
    setenv("ARECA_TEST_MOUSE_HELPER_EXIT", "1", 1);
    fcitx::EventLoop loop;
    areca::MouseClickTracker failing(loop);
    assert(failing.start());
    assert(runUntil(
        loop, [&failing]() { return !failing.start() && noChildLeft(); }));
    assert(!failing.isValid());
    assert(!failing.start());
    assert(noChildLeft());
    unsetenv("ARECA_TEST_MOUSE_HELPER_EXIT");
  }
}
