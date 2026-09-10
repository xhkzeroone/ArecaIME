#include <cassert>
#include <cstdint>

#include "adaptive_wait.h"

int main() {
  areca::AdaptiveWait wait;
  assert(wait.extraWaitMs() == 0);
  assert(wait.effectiveWaitMs(3) == 3);

  auto adjustment = wait.observeTimer(1000, 5999);
  assert(adjustment == areca::AdaptiveWait::Adjustment::None);
  assert(wait.extraWaitMs() == 0);

  adjustment = wait.observeTimer(1000, 6000);
  assert(adjustment == areca::AdaptiveWait::Adjustment::Increased);
  assert(wait.extraWaitMs() == 5);
  assert(wait.effectiveWaitMs(3) == 8);

  adjustment = wait.observeTimer(2000, 9000);
  assert(adjustment == areca::AdaptiveWait::Adjustment::Increased);
  assert(wait.extraWaitMs() == 10);

  for (uint32_t i = 1; i < areca::AdaptiveWait::StableSamplesToDecay; ++i) {
    adjustment = wait.observeTimer(3000, 3000);
    assert(adjustment == areca::AdaptiveWait::Adjustment::None);
  }
  adjustment = wait.observeTimer(3000, 3000);
  assert(adjustment == areca::AdaptiveWait::Adjustment::Decreased);
  assert(wait.extraWaitMs() == 5);

  for (uint32_t i = 0; i < 100; ++i) {
    wait.observeTimer(0, areca::AdaptiveWait::LagThresholdUsec);
  }
  assert(wait.extraWaitMs() == areca::AdaptiveWait::MaxWaitMs);
  assert(wait.effectiveWaitMs(3) == 50);
  assert(wait.effectiveExtraWaitMs(3) == 47);
  assert(wait.effectiveWaitMs(20) == 50);
  assert(wait.effectiveExtraWaitMs(20) == 30);

  // A user-configured base above the adaptive ceiling remains the minimum.
  assert(wait.effectiveWaitMs(75) == 75);
  assert(wait.effectiveExtraWaitMs(75) == 0);
}
