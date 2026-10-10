#pragma once

#include <algorithm>
#include <cstdint>

namespace areca {

class AdaptiveWait {
public:
  enum class Adjustment { None, Increased, Decreased };

  static constexpr uint32_t StepMs = 10;
  static constexpr uint32_t MaxWaitMs = 50;
  static constexpr uint64_t LagThresholdUsec = 5000;
  static constexpr uint32_t StableTransactionsToDecay = 5;

  void beginTransaction() {
    transactionActive_ = true;
    lagObservedInTransaction_ = false;
  }

  void cancelTransaction() {
    transactionActive_ = false;
    lagObservedInTransaction_ = false;
  }

  uint32_t effectiveWaitMs(uint32_t baseWaitMs) const {
    if (baseWaitMs >= MaxWaitMs) {
      return baseWaitMs;
    }
    return std::min(MaxWaitMs, baseWaitMs + extraWaitMs_);
  }

  uint32_t effectiveExtraWaitMs(uint32_t baseWaitMs) const {
    return effectiveWaitMs(baseWaitMs) - baseWaitMs;
  }

  uint32_t effectiveBackspaceDelayMs(uint32_t baseDelayMs) const {
    if (extraWaitMs_ == 0) {
      return baseDelayMs;
    }
    const uint32_t addedDelay = std::min(4U, (extraWaitMs_ + 9) / 10);
    return std::min(5U, baseDelayMs + addedDelay);
  }

  Adjustment observeTimer(uint64_t deadlineUsec, uint64_t firedAtUsec) {
    return observeLateness(deadlineUsec, firedAtUsec, true);
  }

  Adjustment observeSystemTimer(uint64_t deadlineUsec, uint64_t firedAtUsec) {
    return observeLateness(deadlineUsec, firedAtUsec, transactionActive_);
  }

  Adjustment observeAckTimeout() {
    return applyLag(30, true);
  }

  Adjustment observeAckRoundtrip(uint64_t roundtripUsec) {
    if (roundtripUsec >= 4000) {
      const uint32_t latenessMs = static_cast<uint32_t>(roundtripUsec / 1000);
      return applyLag(latenessMs, true);
    }
    return Adjustment::None;
  }

  Adjustment observeSystemStress(uint32_t suggestedWaitMs) {
    return applyLag(suggestedWaitMs, transactionActive_);
  }

  void markSystemStressed() {
    systemStressed_ = true;
  }

  void clearSystemStress() {
    systemStressed_ = false;
  }

  bool isSystemStressed() const {
    return systemStressed_;
  }

  Adjustment completeTransaction() {
    transactionActive_ = false;
    if (lagObservedInTransaction_) {
      lagObservedInTransaction_ = false;
      return Adjustment::None;
    }
    if (systemStressed_) {
      stableTransactions_ = 0;
      return Adjustment::None;
    }
    if (extraWaitMs_ == 0) {
      stableTransactions_ = 0;
      return Adjustment::None;
    }

    if (++stableTransactions_ < StableTransactionsToDecay) {
      return Adjustment::None;
    }
    stableTransactions_ = 0;
    extraWaitMs_ -= std::min(StepMs, extraWaitMs_);
    return Adjustment::Decreased;
  }

  uint32_t extraWaitMs() const { return extraWaitMs_; }

private:
  Adjustment applyLag(uint32_t latenessMs, bool markTransaction) {
    if (markTransaction) {
      lagObservedInTransaction_ = true;
    }
    stableTransactions_ = 0;
    const uint32_t previous = extraWaitMs_;
    const uint32_t clampedLatenessMs = std::min(MaxWaitMs, latenessMs);
    const uint32_t roundedLatenessMs =
        ((clampedLatenessMs + StepMs - 1) / StepMs) * StepMs;
    const uint32_t steppedWaitMs = std::min(MaxWaitMs, extraWaitMs_ + StepMs);
    extraWaitMs_ = std::max(steppedWaitMs, roundedLatenessMs);
    return extraWaitMs_ != previous ? Adjustment::Increased
                                    : Adjustment::None;
  }

  Adjustment observeLateness(uint64_t deadlineUsec, uint64_t firedAtUsec,
                             bool markTransaction) {
    const uint64_t latenessUsec =
        firedAtUsec > deadlineUsec ? firedAtUsec - deadlineUsec : 0;
    if (latenessUsec >= LagThresholdUsec) {
      const uint64_t latenessMs = std::min<uint64_t>(
          MaxWaitMs, latenessUsec / 1000 + (latenessUsec % 1000 != 0));
      return applyLag(static_cast<uint32_t>(latenessMs), markTransaction);
    }

    return Adjustment::None;
  }

  uint32_t extraWaitMs_ = 0;
  uint32_t stableTransactions_ = 0;
  bool transactionActive_ = false;
  bool lagObservedInTransaction_ = false;
  bool systemStressed_ = false;
};

} // namespace areca
