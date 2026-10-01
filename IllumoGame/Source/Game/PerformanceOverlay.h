#pragma once

#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <cstdint>
#include <string>

class IRenderWindow;
class Renderer;

// CSim's own corner performance readout, drawn over every screen (the
// engine's debug overlay is compiled only into development builds). It shows
// frames per second with the average frame time, and the game's memory use,
// each behind its own persisted setting (showFPS, showMemory). Counts refresh
// twice a second so the digits stay readable. Presentation only.
class PerformanceOverlay
{
public:
  static constexpr float kRefreshSeconds = 0.5f;

  PerformanceOverlay();

  void prepare(IRenderWindow* window, Renderer* renderer);
  // deltaSeconds is the frame time; memoryBytes is 0 when unknown.
  void update(float deltaSeconds,
              bool showFps,
              bool showMemory,
              std::uint64_t memoryBytes);

  GameVisual& getVisual() { return m_visual; }
  bool isVisible() const { return m_visual.isVisible(); }
  float fps() const { return m_fps; }
  float frameMilliseconds() const { return m_frameMilliseconds; }
  const std::string& fpsText() const { return m_fpsText; }
  const std::string& memoryText() const { return m_memoryText; }

  static std::string formatMemory(std::uint64_t bytes);

private:
  void rebuild(bool showFps, bool showMemory);

  GameVisual m_visual;
  IRenderWindow* m_window = nullptr;
  Renderer* m_renderer = nullptr;
  float m_windowSeconds = 0.0f;
  int m_windowFrames = 0;
  float m_fps = 0.0f;
  float m_frameMilliseconds = 0.0f;
  std::string m_fpsText = "-- FPS";
  std::string m_memoryText;
};
