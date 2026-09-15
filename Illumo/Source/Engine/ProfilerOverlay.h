#pragma once

#include <Illumo/Engine/FrameProfiler.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Services/KeyCode.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

// DebugModule owns input and lifetime. The visual remains an ordinary token
// drawable; the fixed category order keeps number-key navigation stable.
class ProfilerOverlay
{
public:
  explicit ProfilerOverlay(FrameProfiler& profiler)
    : m_profiler(profiler)
  {
    m_visual.setSpace(PrimitiveSpace::Pixels);
    m_visual.setLayerHint(RenderLayerId::Debug);
  }
  ProfilerOverlay(const ProfilerOverlay&) = delete;
  ProfilerOverlay& operator=(const ProfilerOverlay&) = delete;
  ProfilerOverlay(ProfilerOverlay&&) = delete;
  ProfilerOverlay& operator=(ProfilerOverlay&&) = delete;
  ~ProfilerOverlay() = default;

  void prepare(Renderer* renderer, IRenderWindow* window, Camera* camera)
  {
    m_visual.setWindow(window);
    m_visual.setCamera(camera);
    m_visual.prepare(renderer);
  }

  void setEnabled(bool enabled)
  {
    m_profiler.setEnabled(enabled);
    m_group = -1;
    m_dirty = true;
  }

  bool handleKey(KeyCode key,
                 InputAction action,
                 bool consoleOpen,
                 bool modified)
  {
    if (consoleOpen || modified) {
      return false;
    }
    if (key == KeyCode::F6) {
      if (action == InputAction::Press) {
        setEnabled(!m_profiler.enabled());
      }
      return true;
    }
    if (!m_profiler.enabled() || key < KeyCode::Num0 || key > KeyCode::Num9) {
      return false;
    }
    if (action == InputAction::Press) {
      const int number =
        static_cast<int>(key) - static_cast<int>(KeyCode::Num0);
      if (number == 0) {
        m_group = -1;
      } else if (m_group == -1 && number <= 3) {
        m_group = number - 1;
      }
      m_dirty = true;
    }
    return true;
  }

  int group() const { return m_group; }
  GameVisual& visual() { return m_visual; }

  void captureInput(InputManager& input, bool consoleOpen) const
  {
    if (!m_profiler.enabled() || consoleOpen || input.isControlPressed() ||
        input.isAltPressed() || input.isShiftPressed()) {
      return;
    }
    // Held keys may have no queued event this frame. Capture polling too so
    // navigation cannot change a product's brush or trigger a bound action.
    for (int key = static_cast<int>(KeyCode::Num0);
         key <= static_cast<int>(KeyCode::Num9);
         ++key) {
      input.suppressKeyForFrame(static_cast<KeyCode>(key));
    }
    // GLFW also emits character events for number presses. Do not let a
    // navigation key type into a product text field behind this overlay.
    std::queue<unsigned int>& characters = input.getCharQueue();
    const size_t count = characters.size();
    for (size_t i = 0; i < count; ++i) {
      const unsigned int character = characters.front();
      characters.pop();
      if (character < '0' || character > '9') {
        characters.push(character);
      }
    }
  }

  void update(double dt, float width, float height)
  {
    if (!m_profiler.enabled()) {
      return;
    }
    m_elapsed += std::isfinite(dt) && dt > 0.0 ? dt : 0.0;
    if (!m_dirty && m_elapsed < 0.25 && width == m_width &&
        height == m_height) {
      return;
    }
    m_elapsed = 0.0;
    m_dirty = false;
    m_width = width;
    m_height = height;
    rebuild();
  }

private:
  struct Slice
  {
    const char* name;
    double milliseconds;
  };

  void rebuild()
  {
    static constexpr std::array<const char*, FrameProfiler::kPhaseCount> kNames{
      "Input / hotkeys",       "Camera",
      "Debug / console",       "Product module",
      "Scene preparation",     "Asset pump",
      "Commands / CPU submit", "Present / swap",
      "Frame limiter",         "Other"
    };
    static constexpr std::array<const char*, 4> kGroups{
      "Update", "Rendering", "Presentation / waits", "Other"
    };
    static constexpr std::array<size_t, 5> kBounds{ 0, 4, 7, 9, 10 };
    static constexpr std::array<ColorRgba, 4> kColors{
      ColorRgba{ 185, 224, 91, 255 },
      ColorRgba{ 146, 116, 245, 255 },
      ColorRgba{ 65, 202, 167, 255 },
      ColorRgba{ 237, 173, 81, 255 }
    };
    const FrameProfiler::Sample average = m_profiler.average();
    std::array<Slice, 4> slices{};
    size_t count = 4;
    if (m_group < 0) {
      for (size_t group = 0; group < count; ++group) {
        slices[group].name = kGroups[group];
        for (size_t phase = kBounds[group]; phase < kBounds[group + 1];
             ++phase) {
          slices[group].milliseconds += average[phase];
        }
      }
    } else {
      const size_t group = static_cast<size_t>(m_group);
      count = kBounds[group + 1] - kBounds[group];
      for (size_t i = 0; i < count; ++i) {
        const size_t phase = kBounds[group] + i;
        slices[i] = { kNames[phase], average[phase] };
      }
    }
    double total = 0.0;
    for (size_t i = 0; i < count; ++i) {
      total += slices[i].milliseconds;
    }

    m_visual.clearPrimitives();
    constexpr float kWidth = 470.0f;
    constexpr float kHeight = 430.0f;
    const float scale = std::max(
      0.01f,
      std::min(
        { 1.0f, (m_width - 16.0f) / kWidth, (m_height - 16.0f) / kHeight }));
    Transform2D transform;
    transform.scaleX = scale;
    transform.scaleY = scale;
    transform.x = std::max(0.0f, m_width - kWidth * scale - 8.0f);
    transform.y = 8.0f;
    m_visual.setTransform(transform);
    m_visual.addFilledRect(0, 0, kWidth, kHeight, { 24, 27, 34, 235 });
    m_visual.addOutlineRect(0, 0, kWidth, kHeight, { 108, 120, 139, 255 });
    const std::string title =
      m_group < 0
        ? "Frame"
        : std::string("Frame > ") + kGroups[static_cast<size_t>(m_group)];
    m_visual.addText(title, 16, 12, 17, ColorRgba{});
    std::ostringstream summary;
    summary.imbue(std::locale::classic());
    summary << std::fixed << std::setprecision(2) << total << " ms | "
            << m_profiler.sampleCount() << " frames";
    m_visual.addText(summary.str(), 16, 38, 14, { 178, 190, 209, 255 });

    constexpr double kTau = 6.283185307179586;
    double angle = -kTau / 4.0;
    for (size_t i = 0; i < count; ++i) {
      const double fraction =
        total > 0.0 ? slices[i].milliseconds / total : 0.0;
      const double sweep = fraction * kTau;
      const int segments = static_cast<int>(std::ceil(fraction * 96.0));
      for (int segment = 0; segment < segments; ++segment) {
        const double a =
          angle + sweep * static_cast<double>(segment) / segments;
        const double b =
          angle + sweep * static_cast<double>(segment + 1) / segments;
        m_visual.addFilledTriangle(235,
                                   157,
                                   235 + 155 * static_cast<float>(std::cos(a)),
                                   157 + 86 * static_cast<float>(std::sin(a)),
                                   235 + 155 * static_cast<float>(std::cos(b)),
                                   157 + 86 * static_cast<float>(std::sin(b)),
                                   kColors[i]);
      }
      angle += sweep;
      const float y = 263.0f + static_cast<float>(i) * 25.0f;
      const std::string prefix =
        m_group < 0 && i < 3 ? "[" + std::to_string(i + 1) + "] " : "    ";
      m_visual.addText(prefix + slices[i].name, 16, y, 14, kColors[i]);
      std::ostringstream value;
      value.imbue(std::locale::classic());
      value << std::fixed << std::setprecision(2) << slices[i].milliseconds
            << " ms  " << std::setprecision(1) << fraction * 100.0 << '%';
      m_visual.addText(value.str(), 305, y, 13, kColors[i]);
    }
    if (total <= 0.0) {
      m_visual.addText(
        "Waiting for completed frames", 85, 150, 14, ColorRgba{});
    }
    m_visual.addText("[1-3] drill down   [0] frame   [F6] hide",
                     16,
                     372,
                     13,
                     { 205, 213, 226, 255 });
    m_visual.addText("Main-thread elapsed time; GPU / workers excluded",
                     16,
                     398,
                     12,
                     { 178, 190, 209, 255 });
  }

  FrameProfiler& m_profiler;
  GameVisual m_visual{ 512 };
  int m_group = -1;
  bool m_dirty = true;
  double m_elapsed = 0.0;
  float m_width = 0.0f;
  float m_height = 0.0f;
};
