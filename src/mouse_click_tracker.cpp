#include "mouse_click_tracker.h"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcitx-utils/log.h>
#include <fcntl.h>
#include <memory>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace areca {

namespace {

constexpr uint64_t kReapRetryUsec = 20 * 1000;
constexpr uint64_t kFirstRetryDelayUsec = 1000 * 1000;
constexpr uint64_t kMaxRetryDelayUsec = 5 * 60 * 1000 * 1000ULL;
constexpr uint64_t kStableUptimeUsec = 60 * 1000 * 1000ULL;

bool reaped(pid_t pid) {
  int result;
  do {
    result = waitpid(pid, nullptr, WNOHANG);
  } while (result < 0 && errno == EINTR);
  return result != 0;
}

// waitpid chặn trên event loop sẽ treo Fcitx nếu helper kẹt trong syscall
// không ngắt được. Timer tự giữ mình vì tracker có thể bị huỷ ngay sau đó.
void reapLater(fcitx::EventLoop &loop, pid_t pid) {
  if (reaped(pid))
    return;
  auto holder = std::make_shared<std::unique_ptr<fcitx::EventSourceTime>>();
  try {
    // Accuracy 0 của sd-event là trễ mặc định 250 ms, quá lâu cho việc này.
    *holder = loop.addTimeEvent(
        CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + kReapRetryUsec, 1000,
        [holder, pid](fcitx::EventSourceTime *source, uint64_t) {
          if (!reaped(pid)) {
            source->setNextInterval(kReapRetryUsec);
            source->setOneShot();
            return true;
          }
          auto timer = std::move(*holder);
          return true;
        });
  } catch (const std::exception &error) {
    // Loop đã dừng khi Fcitx thoát: tiến trình cha sắp kết thúc nên zombie tự
    // được thu hồi; không để exception thoát khỏi destructor của tracker.
    FCITX_WARN() << "areca: mouse helper reap deferred pid=" << pid
                 << " error=" << error.what();
    return;
  }
  if (*holder)
    (*holder)->setOneShot();
}

} // namespace

MouseClickTracker::~MouseClickTracker() { stop(); }

bool MouseClickTracker::start() {
  // activate có thể gọi nhiều lần; chỉ tạo lại helper khi pipe cũ đã đóng.
  if (fd_ >= 0)
    return true;
  // Helper chết ngay khi không mở được seat; không spawn lại mỗi lần đổi focus.
  if (fcitx::now(CLOCK_MONOTONIC) < retryAfterUsec_)
    return false;
  stop();
  // Pipe riêng cho cặp cha/con, không cần địa chỉ socket hay xác thực peer.
  // NONBLOCK tránh treo event loop; CLOEXEC tránh truyền fd thừa sang helper.
  int pipefd[2];
  if (pipe2(pipefd, O_CLOEXEC | O_NONBLOCK) < 0) {
    FCITX_WARN() << "areca: mouse tracker pipe failed: "
                 << std::strerror(errno);
    noteFailure();
    return false;
  }
  posix_spawn_file_actions_t actions;
  const int initError = posix_spawn_file_actions_init(&actions);
  if (initError != 0) {
    FCITX_WARN() << "areca: mouse tracker spawn setup failed: "
                 << std::strerror(initError);
    close(pipefd[0]);
    close(pipefd[1]);
    noteFailure();
    return false;
  }
  // Chỉ stdout mang protocol R/C; stderr giữ riêng để log không lẫn vào dữ
  // liệu. posix_spawn chạy helper theo đường dẫn cài đặt, không qua shell.
  int error =
      posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
  char path[] = ARECA_MOUSE_HELPER_PATH;
  char *argv[] = {path, nullptr};
  if (!error)
    error = posix_spawn(&child_, path, &actions, nullptr, argv, environ);
  posix_spawn_file_actions_destroy(&actions);
  // Cha không giữ đầu ghi để read nhận EOF khi helper thoát.
  close(pipefd[1]);
  if (error) {
    FCITX_WARN() << "areca: mouse helper spawn failed path=" << path
                 << " error=" << std::strerror(error);
    close(pipefd[0]);
    child_ = -1;
    noteFailure();
    return false;
  }
  fd_ = pipefd[0];
  startedAtUsec_ = fcitx::now(CLOCK_MONOTONIC);
  // Nhận click trên cùng luồng xử lý phím để không sửa trạng thái engine từ
  // thread khác. Callback chỉ đánh dấu, chưa reset khi rewrite còn dang dở.
  source_ = loop_.addIOEvent(
      fd_, fcitx::IOEventFlags{fcitx::IOEventFlag::In, fcitx::IOEventFlag::Hup},
      [this](fcitx::EventSourceIO *, int, fcitx::IOEventFlags) {
        drain();
        return true;
      });
  if (!source_) {
    FCITX_WARN() << "areca: mouse tracker cannot watch pipe";
    stop();
    noteFailure();
    return false;
  }
  if (debugEnabled())
    FCITX_INFO() << "areca: mouse helper started pid=" << child_;
  return true;
}

void MouseClickTracker::stop() {
  // Gỡ watcher trước khi đóng fd để event loop không truy cập fd đã tái sử
  // dụng.
  source_.reset();
  if (fd_ >= 0)
    close(fd_);
  fd_ = -1;
  if (child_ > 0) {
    if (debugEnabled())
      FCITX_INFO() << "areca: mouse helper cleanup pid=" << child_;
    // Helper không có dữ liệu cần lưu; kết thúc dứt điểm và thu hồi để không
    // để lại zombie khi addon bị hủy hoặc khởi động lại helper.
    kill(child_, SIGKILL);
    reapLater(loop_, child_);
  }
  child_ = -1;
  ready_ = pending_ = false;
}

void MouseClickTracker::drain() {
  if (fd_ < 0)
    return;
  char bytes[256];
  for (;;) {
    const auto count = read(fd_, bytes, sizeof(bytes));
    if (count > 0) {
      for (ssize_t i = 0; i < count; ++i) {
        // R: helper khởi tạo xong; C: có nhấn nút chuột. Nhiều C gộp thành một
        // lần reset vì mục đích là bỏ composition cũ, không đếm số lần click.
        if (bytes[i] == 'R') {
          ready_ = true;
          if (debugEnabled())
            FCITX_INFO() << "areca: mouse helper ready pid=" << child_;
        }
        if (bytes[i] == 'C') {
          if (!pending_ && debugEnabled())
            FCITX_INFO() << "areca: mouse click received; reset pending";
          pending_ = true;
        }
      }
    } else if (count < 0 && errno == EINTR) {
      continue;
    } else {
      if (count == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
        if (count == 0) {
          FCITX_WARN() << "areca: mouse helper disconnected pid=" << child_
                       << "; retry after backoff";
        } else {
          FCITX_WARN() << "areca: mouse tracker read failed: "
                       << std::strerror(errno);
        }
        // Chỉ tắt watcher vì drain có thể đang chạy ngay trong callback của nó.
        // Giữ pending_ để click đã nhận vẫn được xử lý dù helper mất kết nối.
        if (source_)
          source_->setEnabled(false);
        close(fd_);
        fd_ = -1;
        ready_ = false;
        if (child_ > 0) {
          kill(child_, SIGKILL);
          reapLater(loop_, child_);
          child_ = -1;
        }
        noteFailure();
      }
      return;
    }
  }
}

void MouseClickTracker::noteFailure() {
  const uint64_t now = fcitx::now(CLOCK_MONOTONIC);
  // Helper đã chạy ổn lâu rồi mới chết thì coi như lỗi mới, thử lại sớm.
  if (startedAtUsec_ && now - startedAtUsec_ >= kStableUptimeUsec)
    failures_ = 0;
  startedAtUsec_ = 0;
  const uint64_t delay =
      std::min(kFirstRetryDelayUsec << std::min<uint32_t>(failures_, 16),
               kMaxRetryDelayUsec);
  ++failures_;
  retryAfterUsec_ = now + delay;
  if (debugEnabled())
    FCITX_INFO() << "areca: mouse helper retry delay_ms=" << delay / 1000
                 << " failures=" << failures_;
}

bool MouseClickTracker::hasPendingClick() {
  // Phím và pipe có thể cùng sẵn sàng nhưng callback phím chạy trước. Đọc
  // thêm ở đây để lấy click đã đến pipe trước khi quyết định xử lý phím mới.
  drain();
  return pending_;
}

} // namespace areca
