#pragma once

#include <algorithm>
#include <cstdint>

namespace areca {

class AdaptiveWait {
public:
  enum class Adjustment { None, Increased, Decreased };

  static constexpr uint32_t StepMs = 5;
  static constexpr uint32_t MaxWaitMs = 50;
  static constexpr uint64_t LagThresholdUsec = 5000;
  static constexpr uint32_t StableSamplesToDecay = 5;

  uint32_t effectiveWaitMs(uint32_t baseWaitMs) const {
    if (baseWaitMs >= MaxWaitMs) {
      return baseWaitMs;
    }
    return std::min(MaxWaitMs, baseWaitMs + extraWaitMs_);
  }

  uint32_t effectiveExtraWaitMs(uint32_t baseWaitMs) const {
    return effectiveWaitMs(baseWaitMs) - baseWaitMs;
  }

  Adjustment observeTimer(uint64_t deadlineUsec, uint64_t firedAtUsec) {
    const uint64_t latenessUsec =
        firedAtUsec > deadlineUsec ? firedAtUsec - deadlineUsec : 0;
    if (latenessUsec >= LagThresholdUsec) {
      stableSamples_ = 0;
      const uint32_t previous = extraWaitMs_;
      extraWaitMs_ = std::min(MaxWaitMs, extraWaitMs_ + StepMs);
      return extraWaitMs_ != previous ? Adjustment::Increased
                                      : Adjustment::None;
    }

    if (extraWaitMs_ == 0) {
      stableSamples_ = 0;
      return Adjustment::None;
    }

    if (++stableSamples_ < StableSamplesToDecay) {
      return Adjustment::None;
    }
    stableSamples_ = 0;
    extraWaitMs_ -= std::min(StepMs, extraWaitMs_);
    return Adjustment::Decreased;
  }

  uint32_t extraWaitMs() const { return extraWaitMs_; }

private:
  uint32_t extraWaitMs_ = 0;
  uint32_t stableSamples_ = 0;
};

} // namespace areca
