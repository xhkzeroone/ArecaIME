#include <unistd.h>

int main() {
  // Unknown bytes must not cause resets. Multiple presses coalesce.
  if (write(STDOUT_FILENO, "R?CC", 4) != 4) return 1;
  for (;;) pause();
}
