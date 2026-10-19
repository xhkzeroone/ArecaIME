#pragma once

#include <cstdint>

namespace areca {

struct SurroundingReliabilityState {
  static constexpr uint32_t FirstProbeTimeoutMs = 150;
  static constexpr uint32_t RetryProbeTimeoutMs = 80;

  bool known = false;
  bool reliable = false;
  bool forceForwardBackspace = false;
  bool probePending = false;
  uint32_t probeAttempts = 0;
  uint32_t failedAtWordSerial = 0;
  bool retryOnNextWord = false;

  void reset() {
    known = false;
    reliable = false;
    forceForwardBackspace = false;
    probePending = false;
    probeAttempts = 0;
    failedAtWordSerial = 0;
    retryOnNextWord = false;
  }

  void probeStarted() { ++probeAttempts; }

  uint32_t activeProbeTimeoutMs() const {
    return probeAttempts <= 1 ? FirstProbeTimeoutMs : RetryProbeTimeoutMs;
  }

  void probeFailed(uint32_t wordSerial) {
    known = true;
    reliable = false;
    probePending = false;
    failedAtWordSerial = wordSerial;
    retryOnNextWord = probeAttempts < 2;
  }

  bool retryForWord(uint32_t wordSerial) {
    if (!retryOnNextWord || wordSerial <= failedAtWordSerial) {
      return false;
    }
    known = false;
    reliable = false;
    forceForwardBackspace = false;
    probePending = false;
    retryOnNextWord = false;
    return true;
  }
};

} // namespace areca
