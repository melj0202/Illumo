#include <Illumo/Gui/GuiPanelPointer.h>

#include <Illumo/Gui/PanelSurfaces.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/InputManager.h>

void
GuiPanelPointer::sample(const GuiPanelPlacement& placement,
                        IRenderWindow* window,
                        Renderer* renderer,
                        InputManager* input)
{
  const float scale = GuiPanelLayout::viewport(window, renderer).layoutScale;
  const bool detached = placement.surface != IPanelSurfaces::kMainSurface &&
                        placement.surfaces != nullptr;
  PanelSurfacePointer source;
  if (detached) {
    source = placement.surfaces->pointer(placement.surface);
  } else if (input != nullptr) {
    source.left = input->isMouseButtonPressed(KeyCode::MouseLeft);
    source.right = input->isMouseButtonPressed(KeyCode::MouseRight);
  }
  if (m_sampled && placement.surface != m_surface) {
    // The panel moved windows: a held button is not a new press there.
    m_tracker.reset(source.left);
    m_rightDown = source.right;
  }
  m_sampled = true;
  m_surface = placement.surface;
  m_blocked = placement.inputBlocked;
  m_input = input;
  m_mainWheel = !detached;
  if (detached) {
    m_tracker.sample(source, scale);
    m_wheel = source.scroll;
    m_pixelX = static_cast<float>(source.x);
    m_pixelY = static_cast<float>(source.y);
  } else {
    m_tracker.sample(window, input, scale);
    m_wheel = 0.0;
    m_pixelX = m_tracker.x() * scale;
    m_pixelY = m_tracker.y() * scale;
  }
  m_rightClicked = source.right && !m_rightDown;
  m_rightDown = source.right;
}

float
GuiPanelPointer::takeWheel()
{
  if (m_mainWheel) {
    if (m_input == nullptr) {
      return 0.0f;
    }
    double* wheel = m_input->getMouseScrollOffset();
    if (wheel == nullptr) {
      return 0.0f;
    }
    const float steps = static_cast<float>(*wheel);
    *wheel = 0.0;
    return steps;
  }
  const float steps = static_cast<float>(m_wheel);
  m_wheel = 0.0;
  return steps;
}
