#pragma once
#include <Illumo/Engine/PresentationTiming.h>
#include <Illumo/Platform/ProcessMemoryStats.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>

// Main-thread presentation state, independent of the drawable and native query.
class DebugOverlayState
{
public:
  using MemoryQuery = bool (*)(ProcessMemoryStats&) noexcept;

  static constexpr int kFrameTimeCapacity = 180;

  bool update(double dt,
              bool showFps,
              bool showMemory,
              bool vsync,
              int refreshHz,
              long targetFps,
              MemoryQuery query = QueryProcessMemoryStats)
  {
    m_vsync = vsync;
    m_refreshHz = refreshHz;
    m_targetFps = targetFps;
    bool refresh = showFps != m_showFps || showMemory != m_showMemory;
    if (showFps) {
      if (dt > 0.0 && std::isfinite(dt)) {
        pushFrameTime(dt);
        m_fpsElapsed += dt;
        ++m_fpsFrames;
      }
      if (!m_showFps || m_fpsElapsed >= 1.0) {
        if (m_fpsElapsed >= 1.0) {
          m_submitFps = static_cast<int>(
            static_cast<double>(m_fpsFrames) / m_fpsElapsed + 0.5);
          m_fpsElapsed = 0.0;
          m_fpsFrames = 0;
        }
        int p50 = 0;
        int p98 = 0;
        int p995 = 0;
        frameTimePercentiles(&p50, &p98, &p995);
        m_fpsText = buildFrameRateLabel(
          m_submitFps, vsync, refreshHz, targetFps, p50, p98, p995);
        refresh = true;
      }
    } else {
      m_fpsElapsed = 0.0;
      m_fpsFrames = 0;
      m_submitFps = 0;
      m_frameTimeCount = 0;
      m_frameTimeWrite = 0;
    }

    if (showMemory) {
      m_memoryElapsed += dt;
      if (!m_showMemory || m_memoryElapsed >= 1.0) {
        ProcessMemoryStats stats;
        if (query(stats)) {
          std::ostringstream text;
          text.imbue(std::locale::classic());
          constexpr double kBytesPerMiB = 1024.0 * 1024.0;
          text << std::fixed << std::setprecision(1) << "RAM: "
               << static_cast<double>(stats.residentBytes) / kBytesPerMiB
               << " MiB\nPeak RAM: "
               << static_cast<double>(stats.peakResidentBytes) / kBytesPerMiB
               << " MiB\nPrivate commit: "
               << static_cast<double>(stats.privateCommitBytes) / kBytesPerMiB
               << " MiB";
          m_memoryText = text.str();
        } else {
          m_memoryText = "Memory: unavailable";
        }
        m_memoryElapsed = 0.0;
        refresh = true;
      }
    } else {
      m_memoryElapsed = 0.0;
    }

    m_showFps = showFps;
    m_showMemory = showMemory;
    if (!refresh) {
      return false;
    }
    std::string content = showFps ? m_fpsText : std::string{};
    if (showMemory) {
      if (!content.empty()) {
        content += '\n';
      }
      content += m_memoryText;
    }
    if (content == m_content) {
      return false;
    }
    m_content = content;
    return true;
  }

  const std::string& content() const { return m_content; }
  const std::string& frameRateText() const { return m_fpsText; }
  const std::string& memoryText() const { return m_memoryText; }
  bool visible() const { return m_showFps || m_showMemory; }
  bool showsFrameRate() const { return m_showFps; }
  bool vsync() const { return m_vsync; }
  int refreshHz() const { return m_refreshHz; }
  long targetFps() const { return m_targetFps; }
  int frameTimeCount() const { return m_frameTimeCount; }

  // Oldest sample first. Returns how many were written.
  int copyFrameTimes(float* destination, int capacity) const
  {
    if (destination == nullptr || capacity <= 0 || m_frameTimeCount <= 0) {
      return 0;
    }
    const int copied =
      m_frameTimeCount < capacity ? m_frameTimeCount : capacity;
    for (int index = 0; index < copied; ++index) {
      const int age = copied - 1 - index;
      int slot = m_frameTimeWrite - 1 - age;
      if (slot < 0) {
        slot += kFrameTimeCapacity;
      }
      destination[index] = m_frameTimes[slot];
    }
    return copied;
  }

private:
  void pushFrameTime(double dt)
  {
    m_frameTimes[m_frameTimeWrite] = static_cast<float>(dt);
    m_frameTimeWrite += 1;
    if (m_frameTimeWrite >= kFrameTimeCapacity) {
      m_frameTimeWrite = 0;
    }
    if (m_frameTimeCount < kFrameTimeCapacity) {
      m_frameTimeCount += 1;
    }
  }

  // Percentiles of frame time, reported as FPS. A higher percentile is slower.
  void frameTimePercentiles(int* p50, int* p98, int* p995) const
  {
    *p50 = 0;
    *p98 = 0;
    *p995 = 0;
    if (m_frameTimeCount <= 0) {
      return;
    }
    float sorted[kFrameTimeCapacity];
    const int count = copyFrameTimes(sorted, kFrameTimeCapacity);
    std::sort(sorted, sorted + count);
    *p50 = fpsAt(sorted, count, 0.50);
    *p98 = fpsAt(sorted, count, 0.98);
    *p995 = fpsAt(sorted, count, 0.995);
  }

  static int fpsAt(const float* sorted, int count, double percentile)
  {
    if (count <= 0 || sorted == nullptr) {
      return 0;
    }
    const double rank = std::ceil(percentile * static_cast<double>(count - 1));
    int index = static_cast<int>(rank);
    if (index < 0) {
      index = 0;
    }
    if (index >= count) {
      index = count - 1;
    }
    const float seconds = sorted[index];
    if (!(seconds > 0.0f)) {
      return 0;
    }
    return static_cast<int>(1.0 / static_cast<double>(seconds) + 0.5);
  }

  bool m_showFps = false;
  bool m_showMemory = false;
  bool m_vsync = true;
  int m_refreshHz = 0;
  long m_targetFps = 0;
  double m_fpsElapsed = 0.0;
  int m_fpsFrames = 0;
  int m_submitFps = 0;
  double m_memoryElapsed = 0.0;
  int m_frameTimeCount = 0;
  int m_frameTimeWrite = 0;
  float m_frameTimes[kFrameTimeCapacity]{};
  std::string m_fpsText;
  std::string m_memoryText;
  std::string m_content;
};
