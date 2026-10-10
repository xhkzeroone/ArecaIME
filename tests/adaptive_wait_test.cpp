#include <cassert>
#include <cstdint>

#include "adaptive_wait.h"

int main() {
  areca::AdaptiveWait wait;
  assert(wait.extraWaitMs() == 0);
  assert(wait.effectiveWaitMs(3) == 3);

  wait.beginTransaction();
  auto adjustment = wait.observeTimer(1000, 5999);
  assert(adjustment == areca::AdaptiveWait::Adjustment::None);
  assert(wait.extraWaitMs() == 0);
  assert(wait.completeTransaction() == areca::AdaptiveWait::Adjustment::None);

  // A system-wide probe can raise the wait before a Backspace transaction
  // starts. The measured lag is rounded to a 10 ms step.
  adjustment = wait.observeSystemTimer(1000, 13903);
  assert(adjustment == areca::AdaptiveWait::Adjustment::Increased);
  assert(wait.extraWaitMs() == 20);
  assert(wait.effectiveWaitMs(3) == 23);

  // On-time timers inside the same transaction must not decay the wait.
  wait.beginTransaction();
  for (uint32_t i = 0; i < 20; ++i) {
    adjustment = wait.observeTimer(3000, 3000);
    assert(adjustment == areca::AdaptiveWait::Adjustment::None);
  }
  assert(wait.completeTransaction() == areca::AdaptiveWait::Adjustment::None);
  assert(wait.extraWaitMs() == 20);

  // Repeated lag keeps learning in 10 ms steps even when the measured lag is
  // already covered by the current wait.
  wait.beginTransaction();
  adjustment = wait.observeTimer(2000, 9000);
  assert(adjustment == areca::AdaptiveWait::Adjustment::Increased);
  assert(wait.extraWaitMs() == 30);
  assert(wait.completeTransaction() == areca::AdaptiveWait::Adjustment::None);

  // Decay is based on stable transactions, not timer callbacks.
  for (uint32_t i = 1; i < areca::AdaptiveWait::StableTransactionsToDecay;
       ++i) {
    wait.beginTransaction();
    for (uint32_t timer = 0; timer < 10; ++timer) {
      adjustment = wait.observeTimer(3000, 3000);
      assert(adjustment == areca::AdaptiveWait::Adjustment::None);
    }
    assert(wait.completeTransaction() == areca::AdaptiveWait::Adjustment::None);
    assert(wait.extraWaitMs() == 30);
  }
  wait.beginTransaction();
  adjustment = wait.completeTransaction();
  assert(adjustment == areca::AdaptiveWait::Adjustment::Decreased);
  assert(wait.extraWaitMs() == 20);

  for (uint32_t i = 0; i < 100; ++i) {
    wait.beginTransaction();
    wait.observeTimer(0, areca::AdaptiveWait::LagThresholdUsec);
    wait.completeTransaction();
  }
  assert(wait.extraWaitMs() == areca::AdaptiveWait::MaxWaitMs);
  assert(wait.effectiveWaitMs(3) == 50);
  assert(wait.effectiveExtraWaitMs(3) == 47);
  assert(wait.effectiveWaitMs(20) == 50);
  assert(wait.effectiveExtraWaitMs(20) == 30);

  // A user-configured base above the adaptive ceiling remains the minimum.
  assert(wait.effectiveWaitMs(75) == 75);
  assert(wait.effectiveExtraWaitMs(75) == 0);
  assert(wait.effectiveBackspaceDelayMs(1) == 5);

  // Test ACK latency and timeout behavior.
  areca::AdaptiveWait ackWait;
  assert(ackWait.effectiveBackspaceDelayMs(1) == 1);
  ackWait.beginTransaction();
  assert(ackWait.observeAckRoundtrip(1000) == areca::AdaptiveWait::Adjustment::None);
  assert(ackWait.observeAckRoundtrip(6000) == areca::AdaptiveWait::Adjustment::Increased);
  assert(ackWait.extraWaitMs() == 10);
  assert(ackWait.effectiveBackspaceDelayMs(1) == 2);
  assert(ackWait.observeAckTimeout() == areca::AdaptiveWait::Adjustment::Increased);
  assert(ackWait.extraWaitMs() == 30);
  assert(ackWait.effectiveBackspaceDelayMs(1) == 4);

  // Test system stress blocking decay.
  ackWait.markSystemStressed();
  assert(ackWait.isSystemStressed());
  for (uint32_t i = 0; i < 20; ++i) {
    ackWait.beginTransaction();
    assert(ackWait.completeTransaction() == areca::AdaptiveWait::Adjustment::None);
    assert(ackWait.extraWaitMs() == 30);
  }
  ackWait.clearSystemStress();
  assert(!ackWait.isSystemStressed());
  for (uint32_t i = 1; i < areca::AdaptiveWait::StableTransactionsToDecay; ++i) {
    ackWait.beginTransaction();
    assert(ackWait.completeTransaction() == areca::AdaptiveWait::Adjustment::None);
  }
  ackWait.beginTransaction();
  assert(ackWait.completeTransaction() == areca::AdaptiveWait::Adjustment::Decreased);
  assert(ackWait.extraWaitMs() == 20);
}

