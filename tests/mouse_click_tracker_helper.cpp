#include <cstdlib>
#include <unistd.h>

int main() {
  // Giả helper không mở được seat libinput: thoát ngay, không gửi gì.
  if (std::getenv("ARECA_TEST_MOUSE_HELPER_EXIT"))
    return 1;
  // Unknown bytes must not cause resets. Multiple presses coalesce.
  if (write(STDOUT_FILENO, "R?CC", 4) != 4)
    return 1;
  for (;;)
    pause();
}
