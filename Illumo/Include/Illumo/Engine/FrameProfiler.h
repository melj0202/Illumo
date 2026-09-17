#pragma once

#include <array>
#include <chrono>
#include <cstddef>

enum class FramePhase : size_t
{
  Input,
  Camera,
  DebugUpdate,
  ProductUpdate,
  ScenePreparation,
  Assets,
  Commands,
  Presentation,
  Pacing,
  Other,
  Count
};

// Main-thread elapsed time only. Sequential marks partition complete frames;
// background work and GPU execution must never be added to these totals.
class FrameProfiler
{
public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;
  static constexpr size_t kPhaseCount = static_cast<size_t>(FramePhase::Count);
  static constexpr size_t kWindowFrames = 120;
  using Sample = std::array<double, kPhaseCount>;

  void setEnabled(bool enabled)
  {
    if (enabled == m_enabled) {
      return;
    }
    m_enabled = enabled;
    m_recording = false;
    m_history = {};
    m_totals = {};
    m_current = {};
    m_count = 0;
    m_cursor = 0;
  }

  bool enabled() const { return m_enabled; }
  bool recording() const { return m_recording; }
  size_t sampleCount() const { return m_count; }

  void beginFrame()
  {
    if (m_enabled) {
      beginFrame(Clock::now());
    }
  }

  void beginFrame(TimePoint now)
  {
    m_recording = m_enabled;
    m_current = {};
    m_phase = FramePhase::Other;
    m_last = now;
  }

  void mark(FramePhase phase)
  {
    if (m_recording) {
      mark(phase, Clock::now());
    }
  }

  void mark(FramePhase phase, TimePoint now)
  {
    if (!m_recording || phase >= FramePhase::Count || now < m_last) {
      return;
    }
    m_current[static_cast<size_t>(m_phase)] +=
      std::chrono::duration<double, std::milli>(now - m_last).count();
    m_last = now;
    m_phase = phase;
  }

  void endFrame()
  {
    if (m_recording) {
      endFrame(Clock::now());
    }
  }

  void endFrame(TimePoint now)
  {
    if (!m_recording || now < m_last) {
      return;
    }
    mark(FramePhase::Other, now);
    for (size_t i = 0; i < kPhaseCount; ++i) {
      m_totals[i] += m_current[i] - m_history[m_cursor][i];
    }
    m_history[m_cursor] = m_current;
    m_cursor = (m_cursor + 1) % kWindowFrames;
    if (m_count < kWindowFrames) {
      ++m_count;
    }
    m_recording = false;
  }

  Sample average() const
  {
    Sample result{};
    if (m_count != 0) {
      for (size_t i = 0; i < kPhaseCount; ++i) {
        // Clamp subtraction roundoff after ring eviction.
        result[i] =
          m_totals[i] > 0.0 ? m_totals[i] / static_cast<double>(m_count) : 0.0;
      }
    }
    return result;
  }

private:
  bool m_enabled = false;
  bool m_recording = false;
  FramePhase m_phase = FramePhase::Other;
  TimePoint m_last{};
  Sample m_current{};
  Sample m_totals{};
  std::array<Sample, kWindowFrames> m_history{};
  size_t m_count = 0;
  size_t m_cursor = 0;
};
