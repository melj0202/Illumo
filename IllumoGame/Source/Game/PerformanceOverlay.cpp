#include "PerformanceOverlay.h"
#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Primitives/UiTheme.h>
#include <Illumo/Rendering/Renderer.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

// Sits under the canvas's settings button, clear of the inspector (top left).
static const float kMargin = 12.0f;
static const float kTopOffset = 64.0f;
static const float kPadding = 10.0f;
static const float kLineHeight = 17.0f;
static const float kTextSize = 11.5f;

PerformanceOverlay::PerformanceOverlay()
  : m_visual(256u)
{
  m_visual.setSpace(PrimitiveSpace::Pixels);
  m_visual.setLayerHint(RenderLayerId::UI);
  m_visual.setVisible(false);
}

void
PerformanceOverlay::prepare(IRenderWindow* window, Renderer* renderer)
{
  m_window = window;
  m_renderer = renderer;
  m_visual.setWindow(window);
  m_visual.setRenderer(renderer);
  m_visual.prepare(renderer);
}

std::string
PerformanceOverlay::formatMemory(std::uint64_t bytes)
{
  char buffer[48] = {};
  std::snprintf(buffer,
                sizeof(buffer),
                "%.1f MiB",
                static_cast<double>(bytes) / (1024.0 * 1024.0));
  return std::string("Memory ") + buffer;
}

void
PerformanceOverlay::update(float deltaSeconds,
                           bool showFps,
                           bool showMemory,
                           std::uint64_t memoryBytes)
{
  if (std::isfinite(deltaSeconds) && deltaSeconds > 0.0f) {
    m_windowSeconds += deltaSeconds;
    m_windowFrames += 1;
  }
  if (m_windowSeconds >= kRefreshSeconds && m_windowFrames > 0) {
    m_fps = static_cast<float>(m_windowFrames) / m_windowSeconds;
    m_frameMilliseconds =
      m_windowSeconds * 1000.0f / static_cast<float>(m_windowFrames);
    char buffer[48] = {};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%d FPS  %.1f ms",
                  static_cast<int>(std::lround(m_fps)),
                  static_cast<double>(m_frameMilliseconds));
    m_fpsText = buffer;
    m_windowSeconds = 0.0f;
    m_windowFrames = 0;
  }
  m_memoryText =
    memoryBytes > 0u ? formatMemory(memoryBytes) : std::string("Memory --");
  if (!showFps && !showMemory) {
    m_visual.clearPrimitives();
    m_visual.setVisible(false);
    return;
  }
  rebuild(showFps, showMemory);
}

void
PerformanceOverlay::rebuild(bool showFps, bool showMemory)
{
  m_visual.clearPrimitives();
  const float scale =
    m_renderer != nullptr ? std::max(0.01f, m_renderer->getUiScale()) : 1.0f;
  const std::array<int, 2> window = m_window != nullptr
                                      ? m_window->getWindowDimensions()
                                      : std::array<int, 2>{ 1280, 720 };
  const float virtualWidth = static_cast<float>(window[0]) / scale;

  float textWidth = 0.0f;
  if (showFps) {
    textWidth = std::max(
      textWidth, GuiKit::measureEmphasizedText(m_fpsText, kTextSize, 0.0f));
  }
  if (showMemory) {
    textWidth = std::max(
      textWidth, GuiKit::measureEmphasizedText(m_memoryText, kTextSize, 0.0f));
  }
  const int lines = (showFps ? 1 : 0) + (showMemory ? 1 : 0);
  const float width = std::max(96.0f, textWidth + kPadding * 2.0f);
  const float height = kLineHeight * static_cast<float>(lines) + kPadding;
  const float x = std::max(0.0f, virtualWidth - width - kMargin);
  const float y = kTopOffset;
  const float radius = 8.0f;
  GuiKit::drawRoundedRect(
    m_visual, x, y, width, height, radius, UiTheme::glassRim());
  GuiKit::drawRoundedGradientRect(m_visual,
                                  x + 1.0f,
                                  y + 1.0f,
                                  width - 2.0f,
                                  height - 2.0f,
                                  radius - 1.0f,
                                  UiTheme::glassTop(),
                                  UiTheme::glassBottom());
  float lineY = y + kPadding * 0.5f + (kLineHeight - kTextSize) * 0.5f;
  if (showFps) {
    m_visual.addText(
      m_fpsText, x + kPadding, lineY, kTextSize, UiTheme::textPrimary());
    lineY += kLineHeight;
  }
  if (showMemory) {
    m_visual.addText(
      m_memoryText, x + kPadding, lineY, kTextSize, UiTheme::textSecondary());
  }
  m_visual.setVisible(true);
}
