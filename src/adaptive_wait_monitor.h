#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <fcitx-utils/event.h>

#include "adaptive_wait.h"

namespace areca {

class AdaptiveWaitMonitor {
public:
  using DebugProvider = std::function<bool()>;

  static constexpr uint64_t ProbeIntervalUsec = 50'000;
  static constexpr uint64_t TimerAccuracyUsec = 1'000;

  AdaptiveWaitMonitor(fcitx::EventLoop &eventLoop, AdaptiveWait &adaptiveWait,
                      DebugProvider debugProvider);

private:
  void discoverThermalZones();
  void checkSystemHealth();

  fcitx::EventLoop &eventLoop_;
  AdaptiveWait &adaptiveWait_;
  DebugProvider debugProvider_;
  std::unique_ptr<fcitx::EventSourceTime> timer_;
  uint64_t deadlineUsec_ = 0;
  uint64_t lastHealthCheckUsec_ = 0;
  std::vector<std::string> thermalTempPaths_;
  unsigned int hardwareCores_ = 1;
};

} // namespace areca
