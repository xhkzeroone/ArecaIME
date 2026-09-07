#include <cassert>
#include <iostream>
#include <string>

#include <fcitx-utils/event.h>

#include "surrounding_text_v2_backend.h"
#include "types.h"

int main() {
  fcitx::EventLoop eventLoop;
  areca::SurroundingTextV2Backend backend(eventLoop, []() { return false; });

  assert(std::string(backend.name()) == "surrounding-text-v2");
  assert(!backend.hasPending());

  areca::RewritePlan plan;
  assert(plan.surroundingDeleteDelayMs == 0);
  assert(plan.afterSurroundingDeleteWaitMs == 3);

  std::cout << "SurroundingTextV2Backend test passed\n";
  return 0;
}
