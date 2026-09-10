#include <libinput.h>
#include <libudev.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <memory>
#include <poll.h>
#include <unistd.h>

namespace {
// Mở thiết bị bằng quyền user/ACL udev hiện có; helper không tự nâng quyền.
// libinput cần mã lỗi âm, giữ errno trước khi ghi log vì log có thể đổi errno.
int openDevice(const char *path, int flags, void *) {
  const int fd = open(path, flags | O_CLOEXEC);
  if (fd < 0) {
    const int error = errno;
    if (error == EACCES) std::fprintf(stderr, "areca-mouse-monitor: access denied: %s\n", path);
    return -error;
  }
  return fd;
}
void closeDevice(int fd, void *) { close(fd); }
const libinput_interface interface{openDevice, closeDevice};
bool notify(char byte) {
  ssize_t result;
  do { result = write(STDOUT_FILENO, &byte, 1); } while (result < 0 && errno == EINTR);
  // Pipe đầy nghĩa là đã có thông báo chờ đọc. Có thể gộp click vì addon chỉ
  // cần biết phải reset; không chặn dispatch libinput để chờ addon đọc pipe.
  return result == 1 || (result < 0 && errno == EAGAIN);
}
}

int main() {
  std::unique_ptr<udev, decltype(&udev_unref)> devices(udev_new(), udev_unref);
  if (!devices) return 1;
  std::unique_ptr<libinput, decltype(&libinput_unref)> input(
      libinput_udev_create_context(&interface, nullptr, devices.get()), libinput_unref);
  // Theo seat của phiên desktop; udev giúp libinput theo dõi cả thiết bị cắm
  // thêm về sau, không phải tự quét hoặc hard-code /dev/input/eventN.
  const char *seat = std::getenv("XDG_SEAT");
  if (!input || libinput_udev_assign_seat(input.get(), seat && *seat ? seat : "seat0") != 0) {
    std::fprintf(stderr, "areca-mouse-monitor: cannot initialize libinput seat\n");
    return 1;
  }
  // R chỉ báo context libinput đã khởi tạo; stdout dành riêng cho protocol.
  if (!notify('R')) return 1;
  pollfd fds[] = {{libinput_get_fd(input.get()), POLLIN, 0},
                  {STDOUT_FILENO, 0, 0}};
  for (;;) {
    if (libinput_dispatch(input.get()) != 0) return 1;
    while (auto *event = libinput_get_event(input.get())) {
      const auto type = libinput_event_get_type(event);
      bool ok = true;
      if (type == LIBINPUT_EVENT_DEVICE_ADDED) {
        auto *device = libinput_event_get_device(event);
        // Context libinput riêng không dùng cấu hình tap của compositor;
        // bật tap ở đây để chạm touchpad cũng tạo sự kiện nút chuột.
        if (libinput_device_config_tap_get_finger_count(device) > 0)
          libinput_device_config_tap_set_enabled(device, LIBINPUT_CONFIG_TAP_ENABLED);
      } else if (type == LIBINPUT_EVENT_POINTER_BUTTON &&
          libinput_event_pointer_get_button_state(libinput_event_get_pointer_event(event)) ==
              LIBINPUT_BUTTON_STATE_PRESSED) {
        // Chỉ lần nhấn cần bỏ composition. Nhả nút, di chuyển và cuộn không
        // gửi reset; không lọc mã nút để cả click phải/giữa cũng được tính.
        ok = notify('C');
      }
      libinput_event_destroy(event);
      if (!ok) return 1;
    }
    int result;
    do { result = poll(fds, 2, -1); } while (result < 0 && errno == EINTR);
    if (result < 0 || (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))) return 1;
    // Theo dõi cả đầu ghi pipe để thoát khi Fcitx đóng đầu đọc hoặc crash,
    // kể cả khi không còn click nào đánh thức libinput.
    if (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) return 0;
  }
}
