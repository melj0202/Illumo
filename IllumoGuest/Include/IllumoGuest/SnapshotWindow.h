#pragma once
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/UiScale.h>
#include <Illumo/Services/IEnvVars.h>
#include <IllumoGuest/Display.h>
#include <IllumoGuest/Input.h>
#include <stdexcept>

// Guest-local window values. A configured display client submits absolute
// settings and reconciles actual results without exposing native pointers.
class GuestSnapshotWindow final : public IRenderWindow
{
public:
  GuestSnapshotWindow()
    : IRenderWindow(1280, 720, "", nullptr)
  {
  }
  void bindDisplay(GuestDisplay& display, IEnvVars& environment)
  {
    m_display = &display;
    m_environment = &environment;
  }
  // Call after settings bootstrap and each module update. New edits supersede
  // a pending request; only the final completion reconciles the local values.
  void synchronizeDisplay()
  {
    if (m_display == nullptr || m_environment == nullptr) {
      return;
    }
    const EnvVar& fps = m_environment->getVar("fps");
    const EnvVar& scale = m_environment->getVar("uiScale");
    const EnvVar& vsync = m_environment->getVar("vsync");
    // The UI scale preference travels as-is (0 is automatic), clamped to
    // the wire's range and quantized as the host will report it back.
    const float preference = UiScale::stored(scale);
    GuestDisplayState desired;
    desired.fullscreen = m_environment->getVar("fullscreen").valueAsBool;
    desired.vsync = vsync.value.empty() || vsync.valueAsBool;
    desired.fps = static_cast<std::uint32_t>(
      std::clamp(fps.value.empty() ? 60L : fps.valueAsLong, 0L, 1000L));
    desired.uiScale = preference == 0.0f
                        ? 0.0f
                        : GuestDisplayState::quantizedUiScale(
                            std::clamp(preference, 1.0f, 4.0f));
    desired.hideSystemCursor = m_hideSystemCursor;
    // MSAA is saved by the host for its next window; the running window's
    // samples only ever come back in reports.
    const EnvVar& msaa = m_environment->getVar("msaa");
    const long preferredMsaa = msaa.value.empty() ? 4L : msaa.valueAsLong;
    desired.msaa = preferredMsaa >= 0L && preferredMsaa <= 16L &&
                       GuestDisplayState::validMsaa(
                         static_cast<std::uint32_t>(preferredMsaa))
                     ? static_cast<std::uint32_t>(preferredMsaa)
                     : 4u;
    desired.activeMsaa = m_activeMsaa;
    if (!m_requestedDisplay || desired != m_desired) {
      m_display->apply(desired);
      m_desired = desired;
      m_requestedDisplay = true;
    }
    m_display->pump();
    if (m_display->idle() && m_display->ready() && m_display->error().empty()) {
      const GuestDisplayState& actual = m_display->actual();
      m_environment->setVar("fullscreen", actual.fullscreen);
      m_environment->setVar("vsync", actual.vsync);
      m_environment->setVar("fps", actual.fps);
      m_environment->setVar("uiScale", UiScale::text(actual.uiScale));
      m_environment->setVar("msaa", static_cast<long>(actual.msaa));
      m_activeMsaa = actual.activeMsaa;
      m_desired = actual;
    }
  }
  // The running host window's samples, once the host has reported them.
  int getMsaaSamples() const override
  {
    return m_activeMsaa == GuestDisplayState::kUnknownMsaa
             ? -1
             : static_cast<int>(m_activeMsaa);
  }
  void accept(const GuestInput& input) { m_input = input; }
  // Travels with the next display synchronization; not a persisted setting.
  void setSystemCursorHidden(bool hidden) override
  {
    m_hideSystemCursor = hidden;
  }
  bool systemCursorHidden() const { return m_hideSystemCursor; }
  void updateWindow() override {}
  void toggleFullscreen() override
  {
    if (m_display == nullptr || m_environment == nullptr) {
      throw std::logic_error("Display request service required");
    }
    m_environment->setVar("fullscreen",
                          !m_environment->getVar("fullscreen").valueAsBool);
    m_requestedDisplay = false;
    synchronizeDisplay();
  }
  void reinitializeWindow(int, int, const std::string&) override
  {
    throw std::logic_error("Display request service required");
  }
  void reinitializeWindow() override
  {
    throw std::logic_error("Display request service required");
  }
  void handleResize(int width, int height) override
  {
    m_input.width = width;
    m_input.height = height;
  }
  std::array<double, 2> getMouseCoords() override
  {
    return { m_input.mouseX, m_input.mouseY };
  }
  GLFWwindow* getWindowInstance() override { return nullptr; }
  std::array<int, 2> getWindowDimensions() override
  {
    if (m_override[0] > 0 && m_override[1] > 0) {
      return m_override;
    }
    return { static_cast<int>(m_input.width),
             static_cast<int>(m_input.height) };
  }
  // While a surface window records, the renderer lays out for its size.
  void overrideDimensions(int width, int height)
  {
    m_override = { width, height };
  }
  void clearOverride() { m_override = { 0, 0 }; }
  bool shouldWindowClose() override { return m_close; }
  bool isFramePaced() const override { return false; }
  int getRefreshRate() const override { return 0; }
  void swapBuffers() override
  {
    throw std::logic_error("Host owns presentation");
  }
  void requestClose() override { m_close = true; }
  void cancelCloseRequest() override { m_close = false; }

private:
  GuestInput m_input;
  std::array<int, 2> m_override{ 0, 0 };
  bool m_close = false;
  GuestDisplay* m_display = nullptr;
  IEnvVars* m_environment = nullptr;
  GuestDisplayState m_desired;
  bool m_requestedDisplay = false;
  bool m_hideSystemCursor = false;
  std::uint32_t m_activeMsaa = GuestDisplayState::kUnknownMsaa;
};
