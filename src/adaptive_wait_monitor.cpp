#include "adaptive_wait_monitor.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <utility>

#include <fcitx-utils/log.h>

namespace areca {

namespace {

constexpr uint64_t kHealthCheckIntervalUsec = 250'000;
constexpr int kHighTempMilliC = 80'000;
constexpr int kThrottleTempMilliC = 85'000;

bool readFirstLine(const std::string &path, std::string &outLine) {
  std::ifstream f(path);
  if (!f.is_open()) {
    return false;
  }
  return static_cast<bool>(std::getline(f, outLine));
}

} // namespace

AdaptiveWaitMonitor::AdaptiveWaitMonitor(fcitx::EventLoop &eventLoop,
                                         AdaptiveWait &adaptiveWait,
                                         DebugProvider debugProvider)
    : eventLoop_(eventLoop), adaptiveWait_(adaptiveWait),
      debugProvider_(std::move(debugProvider)) {
  hardwareCores_ = std::max(1U, std::thread::hardware_concurrency());
  discoverThermalZones();

  deadlineUsec_ = fcitx::now(CLOCK_MONOTONIC) + ProbeIntervalUsec;
  timer_ = eventLoop_.addTimeEvent(
      CLOCK_MONOTONIC, deadlineUsec_, TimerAccuracyUsec,
      [this](fcitx::EventSourceTime *source, uint64_t) {
        const uint64_t firedAtUsec = fcitx::now(CLOCK_MONOTONIC);
        const auto adjustment =
            adaptiveWait_.observeSystemTimer(deadlineUsec_, firedAtUsec);
        if (debugProvider_() &&
            adjustment == AdaptiveWait::Adjustment::Increased) {
          FCITX_INFO() << "areca: event loop lag detected lateness_us="
                       << (firedAtUsec > deadlineUsec_
                               ? firedAtUsec - deadlineUsec_
                               : 0)
                       << " adaptive_extra_ms=" << adaptiveWait_.extraWaitMs();
        }

        if (firedAtUsec >= lastHealthCheckUsec_ + kHealthCheckIntervalUsec) {
          lastHealthCheckUsec_ = firedAtUsec;
          checkSystemHealth();
        }

        deadlineUsec_ = firedAtUsec + ProbeIntervalUsec;
        source->setTime(deadlineUsec_);
        return true;
      });
}

void AdaptiveWaitMonitor::discoverThermalZones() {
  thermalTempPaths_.clear();
  for (int i = 0; i < 16; ++i) {
    const std::string basePath =
        "/sys/class/thermal/thermal_zone" + std::to_string(i);
    std::string type;
    if (!readFirstLine(basePath + "/type", type)) {
      continue;
    }
    if (type.find("pkg") != std::string::npos ||
        type.find("TCPU") != std::string::npos ||
        type.find("cpu") != std::string::npos ||
        type.find("core") != std::string::npos ||
        type.find("k10temp") != std::string::npos ||
        type.find("acpitz") != std::string::npos) {
      thermalTempPaths_.push_back(basePath + "/temp");
    }
  }
}

void AdaptiveWaitMonitor::checkSystemHealth() {
  bool highCpuPressure = false;
  bool severePressure = false;

  std::string psiLine;
  if (readFirstLine("/proc/pressure/cpu", psiLine)) {
    const auto pos = psiLine.find("avg10=");
    if (pos != std::string::npos) {
      try {
        const double avg10 = std::stod(psiLine.substr(pos + 6));
        if (avg10 >= 15.0) {
          severePressure = true;
          highCpuPressure = true;
        } else if (avg10 >= 5.0) {
          highCpuPressure = true;
        }
      } catch (...) {}
    }
  }

  std::string loadLine;
  if (readFirstLine("/proc/loadavg", loadLine)) {
    double load1 = 0.0;
    int runnable = 0;
    if (std::sscanf(loadLine.c_str(), "%lf %*f %*f %d/", &load1, &runnable) == 2) {
      if (runnable >= static_cast<int>(hardwareCores_ * 2) ||
          load1 >= static_cast<double>(hardwareCores_ * 2.0)) {
        severePressure = true;
        highCpuPressure = true;
      } else if (runnable >= static_cast<int>(hardwareCores_) ||
                 load1 >= static_cast<double>(hardwareCores_ * 1.5)) {
        highCpuPressure = true;
      }
    }
  }

  int maxTempMilliC = 0;
  for (const auto &tempPath : thermalTempPaths_) {
    std::string tempStr;
    if (readFirstLine(tempPath, tempStr)) {
      try {
        const int tempVal = std::stoi(tempStr);
        if (tempVal > maxTempMilliC) {
          maxTempMilliC = tempVal;
        }
      } catch (...) {}
    }
  }

  const bool isThrottlingTemp = maxTempMilliC >= kThrottleTempMilliC;
  const bool isHotTemp = maxTempMilliC >= kHighTempMilliC;

  if (severePressure || isThrottlingTemp || (highCpuPressure && isHotTemp)) {
    adaptiveWait_.markSystemStressed();
    const uint32_t suggestedWaitMs = (severePressure || isThrottlingTemp) ? 30 : 20;
    const auto adj = adaptiveWait_.observeSystemStress(suggestedWaitMs);
    if (debugProvider_() && adj == AdaptiveWait::Adjustment::Increased) {
      FCITX_INFO() << "areca: system thermal or load lag detected temp_mc="
                   << maxTempMilliC << " psi_severe=" << severePressure
                   << " adaptive_extra_ms=" << adaptiveWait_.extraWaitMs();
    }
  } else if (!highCpuPressure && !isHotTemp) {
    adaptiveWait_.clearSystemStress();
  }
}

} // namespace areca
