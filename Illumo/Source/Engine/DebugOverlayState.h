#pragma once
#include <Illumo/Engine/PresentationTiming.h>
#include <Illumo/Platform/ProcessMemoryStats.h>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>

// Main-thread presentation state, independent of the drawable and native query.
class DebugOverlayState
{
public:
  using MemoryQuery = bool (*)(ProcessMemoryStats&) noexcept;

  bool update(double dt,
              bool showFps,
              bool showMemory,
              bool framePaced,
              int pacedFps,
              MemoryQuery query = QueryProcessMemoryStats)
  {
    bool refresh = showFps != m_showFps || showMemory != m_showMemory;
    if (showFps) {
      if (dt > 0.0) {
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
        m_fpsText = buildFrameRateLabel(framePaced, pacedFps, m_submitFps);
        refresh = true;
      }
    } else {
      m_fpsElapsed = 0.0;
      m_fpsFrames = 0;
      m_submitFps = 0;
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
  bool visible() const { return m_showFps || m_showMemory; }

private:
  bool m_showFps = false;
  bool m_showMemory = false;
  double m_fpsElapsed = 0.0;
  int m_fpsFrames = 0;
  int m_submitFps = 0;
  double m_memoryElapsed = 0.0;
  std::string m_fpsText;
  std::string m_memoryText;
  std::string m_content;
};
