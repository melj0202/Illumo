#pragma once

#include <Illumo/Gui/GuiToolStyle.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/RenderPipelineConfig.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/IEnvVars.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Services/KeyCode.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// Floating developer panel for tweaking live graphics parameters (HDR,
// tonemapping, exposure, FXAA, bloom, motion blur, shadow atlas/bias overrides,
// VSync, and frame pacing). Composed strictly with GuiToolStyle rectangles,
// lines, and font text so the identical GameVisual renders both in-window on
// RenderLayerId::Debug and in a detached OS PixelWindow via SoftwareCanvas.
class GraphicsSettingsOverlay
{
public:
  enum class WindowRequestKind : unsigned char
  {
    None,
    Detach,
    Dock,
    Close
  };

  struct WindowRequest
  {
    WindowRequestKind kind = WindowRequestKind::None;
    int originX = 0;
    int originY = 0;
    int width = 380;
    int height = 560;
  };

  static constexpr float kDefaultWidth = 380.0f;
  static constexpr float kDefaultHeight = 560.0f;
  static constexpr float kMinWidth = 310.0f;
  static constexpr float kMinHeight = 260.0f;
  static constexpr float kRowHeight = 22.0f;
  static constexpr float kFooterHeight = 32.0f;
  static constexpr float kGripSize = 14.0f;
  static constexpr int kRowCount = 29;

  GraphicsSettingsOverlay()
  {
    m_visual.setSpace(PrimitiveSpace::Pixels);
    m_visual.setLayerHint(RenderLayerId::Debug);
  }
  GraphicsSettingsOverlay(const GraphicsSettingsOverlay&) = delete;
  GraphicsSettingsOverlay& operator=(const GraphicsSettingsOverlay&) = delete;
  GraphicsSettingsOverlay(GraphicsSettingsOverlay&&) = delete;
  GraphicsSettingsOverlay& operator=(GraphicsSettingsOverlay&&) = delete;
  ~GraphicsSettingsOverlay() = default;

  void prepare(Renderer* renderer,
               IRenderWindow* window,
               Camera* camera,
               IEnvVars* envVars)
  {
    m_renderer = renderer;
    m_window = window;
    m_envVars = envVars;
    m_visual.setWindow(window);
    m_visual.setCamera(camera);
    m_visual.prepare(renderer);
    m_dirty = true;
  }

  void setDetachAvailable(bool available)
  {
    if (m_detachAvailable != available) {
      m_detachAvailable = available;
      m_dirty = true;
    }
  }

  bool isOpen() const { return m_open; }
  bool isDetached() const { return m_detached; }
  bool wantsInWindowDraw() const { return m_open && !m_detached; }

  void setOpen(bool open)
  {
    if (m_open == open) {
      return;
    }
    m_open = open;
    m_dragMode = DragMode::None;
    m_activeRow = -1;
    if (!m_open) {
      m_visual.clearPrimitives();
      m_visual.setVisible(false);
    } else {
      m_visual.setVisible(!m_detached);
      m_dirty = true;
    }
  }

  void toggle()
  {
    if (m_detached) {
      m_pendingRequest = WindowRequest{ WindowRequestKind::Close, 0, 0, 0, 0 };
      return;
    }
    setOpen(!m_open);
  }

  void requestDetach(float uiScale)
  {
    if (!m_detachAvailable) {
      return;
    }
    if (!m_open) {
      setOpen(true);
    }
    const float safeScale = uiScale > 0.0f ? uiScale : 1.0f;
    ensureInitialPosition();
    WindowRequest req;
    req.kind = WindowRequestKind::Detach;
    req.originX = static_cast<int>(std::lround(m_x * safeScale));
    req.originY = static_cast<int>(std::lround(m_y * safeScale));
    req.width =
      std::max(320, static_cast<int>(std::lround(m_width * safeScale)));
    req.height =
      std::max(280, static_cast<int>(std::lround(m_height * safeScale)));
    m_pendingRequest = req;
  }

  void requestDock()
  {
    if (m_detached) {
      m_pendingRequest = WindowRequest{ WindowRequestKind::Dock, 0, 0, 0, 0 };
    } else {
      setOpen(true);
    }
  }

  void setDetached(bool detached, int width, int height)
  {
    m_detached = detached;
    m_dragMode = DragMode::None;
    m_activeRow = -1;
    m_hoverTitleButton = -1;
    m_hoverRow = -1;
    m_hoverChoice = -1;
    m_hoverFooterButton = -1;
    if (detached) {
      m_open = true;
      m_detachedWidth = static_cast<float>(std::max(1, width));
      m_detachedHeight = static_cast<float>(std::max(1, height));
      m_visual.setVisible(false);
    } else {
      m_visual.setVisible(m_open);
    }
    m_dirty = true;
  }

  void setDetachedSize(int width, int height)
  {
    const float w = static_cast<float>(std::max(1, width));
    const float h = static_cast<float>(std::max(1, height));
    if (w != m_detachedWidth || h != m_detachedHeight) {
      m_detachedWidth = w;
      m_detachedHeight = h;
      clampScroll();
      m_dirty = true;
    }
  }

  WindowRequest takeWindowRequest()
  {
    const WindowRequest req = m_pendingRequest;
    m_pendingRequest = WindowRequest{};
    return req;
  }

  GuiToolRect panelRect() const
  {
    if (m_detached) {
      return GuiToolRect{ 0.0f, 0.0f, m_detachedWidth, m_detachedHeight };
    }
    return GuiToolRect{ m_x, m_y, m_width, m_height };
  }

  bool handleKey(KeyCode key,
                 InputAction action,
                 bool consoleOpen,
                 bool hasModifier,
                 bool f7BoundByUser)
  {
    if (consoleOpen || hasModifier) {
      return false;
    }
    if (key == KeyCode::F7 && !f7BoundByUser) {
      if (action == InputAction::Press) {
        toggle();
      }
      return true;
    }
    if (m_open && !m_detached && key == KeyCode::Escape) {
      if (action == InputAction::Press) {
        setOpen(false);
      }
      return true;
    }
    return false;
  }

  // Updates in-window hover/drag/scroll and rebuilds GameVisual when dirty.
  // Returns true when the pointer interaction belongs to this panel so the
  // caller can mask mouse buttons from the underlying scene.
  bool updateInWindow(double dt,
                      float viewportWidth,
                      float viewportHeight,
                      float mouseX,
                      float mouseY,
                      bool leftDown,
                      double* scrollOffset,
                      bool consoleOpen)
  {
    tickStatus(dt);
    m_viewportWidth = std::max(1.0f, viewportWidth);
    m_viewportHeight = std::max(1.0f, viewportHeight);
    if (!m_open || m_detached) {
      m_wasLeftDown = leftDown;
      m_outsidePressHeld = false;
      return false;
    }

    ensureInitialPosition();
    clampPanelToViewport();
    syncObservedState();

    bool consumedMouse = false;
    if (consoleOpen) {
      clearHover();
      m_dragMode = DragMode::None;
      m_wasLeftDown = leftDown;
      m_outsidePressHeld = false;
    } else {
      const GuiToolRect box = panelRect();
      const bool inside = box.contains(mouseX, mouseY);
      if (scrollOffset != nullptr && *scrollOffset != 0.0 && inside) {
        if (handleScroll(*scrollOffset)) {
          *scrollOffset = 0.0;
        } else {
          *scrollOffset = 0.0;
        }
        consumedMouse = true;
      }

      const bool pressEdge = leftDown && !m_wasLeftDown;
      const bool releaseEdge = !leftDown && m_wasLeftDown;
      m_wasLeftDown = leftDown;

      if (pressEdge) {
        if (inside) {
          m_outsidePressHeld = false;
          handlePointerPress(mouseX, mouseY);
          consumedMouse = true;
        } else {
          m_outsidePressHeld = true;
        }
      } else if (leftDown) {
        if (!m_outsidePressHeld && m_dragMode != DragMode::None) {
          handlePointerDrag(mouseX, mouseY);
          consumedMouse = true;
        } else if (!m_outsidePressHeld && inside) {
          consumedMouse = true;
        }
      } else if (releaseEdge) {
        if (m_dragMode != DragMode::None) {
          consumedMouse = true;
        }
        m_dragMode = DragMode::None;
        m_activeRow = -1;
        m_outsidePressHeld = false;
      }

      if (!leftDown && !m_outsidePressHeld) {
        updateHover(mouseX, mouseY);
      }
    }

    if (m_dirty) {
      m_dirty = false;
      compose();
    }
    m_visual.setVisible(m_open && !m_detached);
    return consumedMouse;
  }

  // Detached OS window input handlers (coordinates in PixelWindow client
  // space).
  void tickDetached(double dt)
  {
    tickStatus(dt);
    syncObservedState();
  }

  void handleDetachedMousePress(double x, double y)
  {
    handlePointerPress(static_cast<float>(x), static_cast<float>(y));
  }

  void handleDetachedMouseMove(double x, double y, bool leftDown)
  {
    const float fx = static_cast<float>(x);
    const float fy = static_cast<float>(y);
    if (leftDown && m_dragMode != DragMode::None) {
      handlePointerDrag(fx, fy);
    } else if (!leftDown) {
      updateHover(fx, fy);
    }
  }

  void handleDetachedMouseRelease()
  {
    m_dragMode = DragMode::None;
    m_activeRow = -1;
  }

  void handleDetachedScroll(double delta) { (void)handleScroll(delta); }

  // Rebuilds m_visual when dirty and returns true if SoftwareCanvas must
  // redraw.
  bool composeDetached()
  {
    if (!m_dirty) {
      return false;
    }
    m_dirty = false;
    compose();
    return true;
  }

  void resetDefaults()
  {
    if (m_envVars != nullptr) {
      m_envVars->setVar("hdrEnabled", true);
      m_envVars->setVar("tonemapper", 2);
      m_envVars->setVar("exposure", "0");
      m_envVars->setVar("fxaaEnabled", true);
      m_envVars->setVar("bloomEnabled", true);
      m_envVars->setVar("bloomIntensity", "0.04");
      m_envVars->setVar("bloomMipCount", 5);
      m_envVars->setVar("canvasBloomEnabled", false);
      m_envVars->setVar("motionBlurEnabled", false);
      m_envVars->setVar("motionBlurAmount", "0.5");
      m_envVars->setVar("motionBlurMax", "0.2");
      m_envVars->setVar("motionBlurSamples", 8);
      m_envVars->setVar("shadowsEnabled", true);
      m_envVars->setVar("shadowPcf", true);
      m_envVars->setVar("shadowCascades", 3);
      m_envVars->setVar("shadowMapSize", 1024);
      m_envVars->setVar("shadowBias", "0.001");
      m_envVars->setVar("shadowSlopeScale", "0.004");
      m_envVars->setVar("shadowNormalOffset", "0.015");
      m_envVars->setVar("vsync", true);
      m_envVars->setVar("fps", 60);
    }
    m_shadowOverrideActive = false;
    if (m_renderer != nullptr) {
      Renderer::ShadowDebugOverrides cleared{};
      cleared.active = false;
      m_renderer->setShadowDebugOverrides(cleared);
    }
    setStatus("Defaults restored");
    m_dirty = true;
  }

  bool shadowOverrideActive() const { return m_shadowOverrideActive; }
  const std::string& statusText() const { return m_statusText; }
  void flushPendingCompose()
  {
    if (wantsInWindowDraw() && m_dirty) {
      ensureInitialPosition();
      clampPanelToViewport();
      syncObservedState();
      m_dirty = false;
      compose();
      m_visual.setVisible(true);
    }
  }
  GameVisual& visual() { return m_visual; }
  const GameVisual& visual() const { return m_visual; }

private:
  enum class DragMode : unsigned char
  {
    None,
    MovePanel,
    ResizePanel,
    Scrollbar,
    Slider
  };

  enum class RowId : int
  {
    HeaderHdr = 0,
    HdrEnabled = 1,
    Tonemapper = 2,
    TonemapperBright = 3,
    Exposure = 4,
    Fxaa = 5,
    HeaderBloom = 6,
    BloomEnabled = 7,
    BloomIntensity = 8,
    BloomMips = 9,
    CanvasBloom = 10,
    HeaderMotionBlur = 11,
    MotionBlurEnabled = 12,
    MotionBlurAmount = 13,
    MotionBlurMax = 14,
    MotionBlurSamples = 15,
    HeaderShadows = 16,
    ShadowOverride = 17,
    ShadowsEnabled = 18,
    ShadowPcf = 19,
    ShadowCascades = 20,
    ShadowMapSize = 21,
    ShadowBias = 22,
    ShadowSlopeScale = 23,
    ShadowNormalOffset = 24,
    HeaderDisplay = 25,
    Vsync = 26,
    FpsCap = 27,
    ShowFps = 28
  };

  enum class RowKind : unsigned char
  {
    Section,
    Toggle,
    Slider,
    Choice
  };

  struct ObservedSnapshot
  {
    RenderPipelineConfig pipeline{};
    bool vsync = true;
    long fps = 60;
    bool showFps = false;
    bool shadowOverride = false;

    bool operator==(const ObservedSnapshot&) const = default;
  };

  Renderer* m_renderer = nullptr;
  IRenderWindow* m_window = nullptr;
  IEnvVars* m_envVars = nullptr;
  GameVisual m_visual{ 4096 };

  bool m_open = false;
  bool m_detached = false;
  bool m_detachAvailable = false;
  bool m_positionInitialized = false;
  bool m_dirty = true;
  bool m_wasLeftDown = false;
  bool m_outsidePressHeld = false;
  bool m_shadowOverrideActive = false;

  float m_x = 24.0f;
  float m_y = 48.0f;
  float m_width = kDefaultWidth;
  float m_height = kDefaultHeight;
  float m_viewportWidth = 1280.0f;
  float m_viewportHeight = 720.0f;
  float m_detachedWidth = kDefaultWidth;
  float m_detachedHeight = kDefaultHeight;

  DragMode m_dragMode = DragMode::None;
  int m_activeRow = -1;
  float m_dragOffsetX = 0.0f;
  float m_dragOffsetY = 0.0f;
  float m_dragStartW = 0.0f;
  float m_dragStartH = 0.0f;
  float m_dragStartX = 0.0f;
  float m_dragStartY = 0.0f;

  int m_firstRow = 0;
  int m_hoverTitleButton = -1;
  int m_hoverRow = -1;
  int m_hoverChoice = -1;
  int m_hoverFooterButton = -1;

  std::string m_statusText;
  double m_statusRemaining = 0.0;
  ObservedSnapshot m_observed{};
  bool m_hasObserved = false;
  WindowRequest m_pendingRequest{};

  void setStatus(const std::string& message)
  {
    m_statusText = message;
    m_statusRemaining = 2.5;
    m_dirty = true;
  }

  void tickStatus(double dt)
  {
    if (m_statusRemaining > 0.0 && std::isfinite(dt) && dt > 0.0) {
      m_statusRemaining -= dt;
      if (m_statusRemaining <= 0.0) {
        m_statusRemaining = 0.0;
        m_statusText.clear();
        m_dirty = true;
      }
    }
  }

  void ensureInitialPosition()
  {
    if (m_positionInitialized) {
      return;
    }
    m_positionInitialized = true;
    m_width = std::clamp(
      kDefaultWidth, kMinWidth, std::max(kMinWidth, m_viewportWidth - 24.0f));
    m_height = std::clamp(kDefaultHeight,
                          kMinHeight,
                          std::max(kMinHeight, m_viewportHeight - 64.0f));
    m_x = std::max(12.0f, m_viewportWidth - m_width - 16.0f);
    m_y = 48.0f;
  }

  void clampPanelToViewport()
  {
    const float maxW = std::max(kMinWidth, m_viewportWidth - 16.0f);
    const float maxH = std::max(kMinHeight, m_viewportHeight - 16.0f);
    if (m_width > maxW) {
      m_width = maxW;
      m_dirty = true;
    }
    if (m_height > maxH) {
      m_height = maxH;
      m_dirty = true;
    }
    const float clampedX =
      std::clamp(m_x, 4.0f, std::max(4.0f, m_viewportWidth - m_width - 4.0f));
    const float clampedY = std::clamp(
      m_y,
      4.0f,
      std::max(4.0f, m_viewportHeight - GuiToolStyle::kTitleHeight - 4.0f));
    if (clampedX != m_x || clampedY != m_y) {
      m_x = clampedX;
      m_y = clampedY;
      m_dirty = true;
    }
    clampScroll();
  }

  ObservedSnapshot readSnapshot() const
  {
    ObservedSnapshot snap;
    snap.pipeline = RenderPipelineConfig::fromEnv(m_envVars);
    snap.shadowOverride = m_shadowOverrideActive;
    if (m_envVars != nullptr) {
      const EnvVar& vsync = m_envVars->getVar("vsync");
      snap.vsync = vsync.value.empty() || vsync.valueAsBool;
      const EnvVar& fps = m_envVars->getVar("fps");
      snap.fps =
        fps.value.empty() ? 60L : std::clamp(fps.valueAsLong, 0L, 1000L);
      snap.showFps = m_envVars->getVar("showFPS").valueAsBool;
    }
    return snap;
  }

  void syncObservedState()
  {
    const ObservedSnapshot current = readSnapshot();
    if (!m_hasObserved || !(current == m_observed)) {
      m_observed = current;
      m_hasObserved = true;
      if (m_shadowOverrideActive) {
        pushShadowOverridesToRenderer(current.pipeline);
      }
      m_dirty = true;
    }
  }

  void pushShadowOverridesToRenderer(const RenderPipelineConfig& cfg)
  {
    if (m_renderer == nullptr) {
      return;
    }
    Renderer::ShadowDebugOverrides overrides{};
    overrides.active = m_shadowOverrideActive;
    overrides.shadowsEnabled = cfg.shadowsEnabled;
    overrides.shadowPcf = cfg.shadowPcf;
    overrides.shadowMapSize = cfg.shadowMapSize;
    overrides.shadowBias = cfg.shadowBias;
    overrides.shadowSlopeScale = cfg.shadowSlopeScale;
    overrides.shadowNormalOffset = cfg.shadowNormalOffset;
    m_renderer->setShadowDebugOverrides(overrides);
  }

  std::vector<GuiToolTitleButton> titleButtons() const
  {
    std::vector<GuiToolTitleButton> buttons;
    buttons.push_back(GuiToolTitleButton::Hide);
    if (m_detached) {
      buttons.push_back(GuiToolTitleButton::Dock);
    } else if (m_detachAvailable) {
      buttons.push_back(GuiToolTitleButton::PopOut);
    }
    return buttons;
  }

  GuiToolRect titleBarRect(const GuiToolRect& panel) const
  {
    return GuiToolRect{ panel.x, panel.y, panel.w, GuiToolStyle::kTitleHeight };
  }

  GuiToolRect footerBarRect(const GuiToolRect& panel) const
  {
    return GuiToolRect{
      panel.x, panel.y + panel.h - kFooterHeight, panel.w, kFooterHeight
    };
  }

  GuiToolRect bodyRect(const GuiToolRect& panel) const
  {
    const float top = panel.y + GuiToolStyle::kTitleHeight + 2.0f;
    const float bottom = panel.y + panel.h - kFooterHeight - 2.0f;
    return GuiToolRect{ panel.x + 1.0f,
                        top,
                        std::max(1.0f, panel.w - 2.0f),
                        std::max(1.0f, bottom - top) };
  }

  int visibleRowCount() const
  {
    const GuiToolRect body = bodyRect(panelRect());
    const int count = static_cast<int>(std::floor(body.h / kRowHeight));
    return std::max(1, count);
  }

  int maxFirstRow() const { return std::max(0, kRowCount - visibleRowCount()); }

  void clampScroll() { m_firstRow = std::clamp(m_firstRow, 0, maxFirstRow()); }

  bool needsScrollbar() const { return visibleRowCount() < kRowCount; }

  GuiToolRect scrollbarTrackRect(const GuiToolRect& body) const
  {
    return GuiToolRect{ body.x + body.w - GuiToolStyle::kScrollbar - 2.0f,
                        body.y + 2.0f,
                        GuiToolStyle::kScrollbar,
                        std::max(8.0f, body.h - 4.0f) };
  }

  GuiToolRect rowContentRect(const GuiToolRect& body, int visibleSlot) const
  {
    const float scrollReserve =
      needsScrollbar() ? (GuiToolStyle::kScrollbar + 4.0f) : 0.0f;
    return GuiToolRect{ body.x,
                        body.y + static_cast<float>(visibleSlot) * kRowHeight,
                        std::max(40.0f, body.w - scrollReserve),
                        kRowHeight };
  }

  GuiToolRect footerButtonRect(const GuiToolRect& footer, int index) const
  {
    const float y = footer.y + 5.0f;
    const float h = footer.h - 10.0f;
    if (index == 0) {
      return GuiToolRect{ footer.x + GuiToolStyle::kPad, y, 128.0f, h };
    }
    return GuiToolRect{ footer.x + GuiToolStyle::kPad + 134.0f, y, 64.0f, h };
  }

  GuiToolRect resizeGripRect(const GuiToolRect& panel) const
  {
    return GuiToolRect{ panel.x + panel.w - kGripSize,
                        panel.y + panel.h - kGripSize,
                        kGripSize,
                        kGripSize };
  }

  static RowKind rowKind(RowId id)
  {
    switch (id) {
      case RowId::HeaderHdr:
      case RowId::HeaderBloom:
      case RowId::HeaderMotionBlur:
      case RowId::HeaderShadows:
      case RowId::HeaderDisplay:
        return RowKind::Section;
      case RowId::HdrEnabled:
      case RowId::Fxaa:
      case RowId::BloomEnabled:
      case RowId::CanvasBloom:
      case RowId::MotionBlurEnabled:
      case RowId::ShadowOverride:
      case RowId::ShadowsEnabled:
      case RowId::ShadowPcf:
      case RowId::Vsync:
      case RowId::ShowFps:
        return RowKind::Toggle;
      case RowId::Exposure:
      case RowId::BloomIntensity:
      case RowId::MotionBlurAmount:
      case RowId::MotionBlurMax:
      case RowId::MotionBlurSamples:
      case RowId::ShadowBias:
      case RowId::ShadowSlopeScale:
      case RowId::ShadowNormalOffset:
        return RowKind::Slider;
      case RowId::Tonemapper:
      case RowId::TonemapperBright:
      case RowId::BloomMips:
      case RowId::ShadowCascades:
      case RowId::ShadowMapSize:
      case RowId::FpsCap:
        return RowKind::Choice;
    }
    return RowKind::Section;
  }

  static const char* rowLabel(RowId id)
  {
    switch (id) {
      case RowId::HeaderHdr:
        return "HDR & TONEMAPPING";
      case RowId::HdrEnabled:
        return "HDR Forward Pipeline";
      case RowId::Tonemapper:
        return "Tonemapper";
      case RowId::TonemapperBright:
        return "";
      case RowId::Exposure:
        return "Exposure (EV)";
      case RowId::Fxaa:
        return "FXAA Anti-Aliasing";
      case RowId::HeaderBloom:
        return "BLOOM";
      case RowId::BloomEnabled:
        return "Bloom";
      case RowId::BloomIntensity:
        return "Intensity";
      case RowId::BloomMips:
        return "Mip Pyramid";
      case RowId::CanvasBloom:
        return "2D Canvas Bloom";
      case RowId::HeaderMotionBlur:
        return "MOTION BLUR";
      case RowId::MotionBlurEnabled:
        return "Motion Blur";
      case RowId::MotionBlurAmount:
        return "Amount";
      case RowId::MotionBlurMax:
        return "Max Velocity";
      case RowId::MotionBlurSamples:
        return "Samples";
      case RowId::HeaderShadows:
        return "SHADOWS";
      case RowId::ShadowOverride:
        return "Override Scene Shadows";
      case RowId::ShadowsEnabled:
        return "Directional & Spot Shadows";
      case RowId::ShadowPcf:
        return "Soft PCF Filtering";
      case RowId::ShadowCascades:
        return "CSM Cascades";
      case RowId::ShadowMapSize:
        return "Map Size";
      case RowId::ShadowBias:
        return "Depth Bias";
      case RowId::ShadowSlopeScale:
        return "Slope Scale";
      case RowId::ShadowNormalOffset:
        return "Normal Bias";
      case RowId::HeaderDisplay:
        return "DISPLAY & PACING";
      case RowId::Vsync:
        return "VSync";
      case RowId::FpsCap:
        return "FPS Cap";
      case RowId::ShowFps:
        return "FPS & Frame Graph (F3)";
    }
    return "";
  }

  static int choiceCount(RowId id)
  {
    switch (id) {
      case RowId::Tonemapper:
        return 3;
      case RowId::TonemapperBright:
        return 4;
      case RowId::BloomMips:
        return 5;
      case RowId::ShadowCascades:
        return 3;
      case RowId::ShadowMapSize:
        return 4;
      case RowId::FpsCap:
        return 6;
      default:
        return 0;
    }
  }

  static const char* choiceLabel(RowId id, int choiceIndex)
  {
    if (id == RowId::Tonemapper) {
      static constexpr const char* kTonemappers[3] = { "None",
                                                       "Reinhard",
                                                       "AgX" };
      return kTonemappers[std::clamp(choiceIndex, 0, 2)];
    }
    if (id == RowId::TonemapperBright) {
      static constexpr const char* kBrightTonemappers[4] = {
        "Neutral", "Punchy", "ACES", "GT"
      };
      return kBrightTonemappers[std::clamp(choiceIndex, 0, 3)];
    }
    if (id == RowId::BloomMips) {
      static constexpr const char* kMips[5] = { "1", "2", "3", "4", "5" };
      return kMips[std::clamp(choiceIndex, 0, 4)];
    }
    if (id == RowId::ShadowCascades) {
      static constexpr const char* kCascades[3] = { "1", "2", "3" };
      return kCascades[std::clamp(choiceIndex, 0, 2)];
    }
    if (id == RowId::ShadowMapSize) {
      static constexpr const char* kSizes[4] = {
        "512", "1024", "2048", "4096"
      };
      return kSizes[std::clamp(choiceIndex, 0, 3)];
    }
    if (id == RowId::FpsCap) {
      static constexpr const char* kCaps[6] = { "Off", "30",  "60",
                                                "120", "144", "240" };
      return kCaps[std::clamp(choiceIndex, 0, 5)];
    }
    return "";
  }

  GuiToolRect choiceButtonRect(const GuiToolRect& rowRect,
                               int count,
                               int index) const
  {
    const float labelWidth = std::min(108.0f, rowRect.w * 0.34f);
    const float startX = rowRect.x + GuiToolStyle::kPad + labelWidth;
    const float availW =
      std::max(32.0f, rowRect.w - labelWidth - GuiToolStyle::kPad * 2.0f);
    const float gap = 3.0f;
    const float btnW =
      (availW - gap * static_cast<float>(std::max(0, count - 1))) /
      static_cast<float>(std::max(1, count));
    return GuiToolRect{ startX + static_cast<float>(index) * (btnW + gap),
                        rowRect.y + 2.0f,
                        std::max(12.0f, btnW),
                        rowRect.h - 4.0f };
  }

  bool readToggleValue(RowId id, const ObservedSnapshot& snap) const
  {
    switch (id) {
      case RowId::HdrEnabled:
        return snap.pipeline.hdrEnabled;
      case RowId::Fxaa:
        return snap.pipeline.fxaaEnabled;
      case RowId::BloomEnabled:
        return snap.pipeline.bloomEnabled;
      case RowId::CanvasBloom:
        return snap.pipeline.canvasBloomEnabled;
      case RowId::MotionBlurEnabled:
        return snap.pipeline.motionBlurEnabled;
      case RowId::ShadowOverride:
        return m_shadowOverrideActive;
      case RowId::ShadowsEnabled:
        return snap.pipeline.shadowsEnabled;
      case RowId::ShadowPcf:
        return snap.pipeline.shadowPcf;
      case RowId::Vsync:
        return snap.vsync;
      case RowId::ShowFps:
        return snap.showFps;
      default:
        return false;
    }
  }

  void applyToggle(RowId id)
  {
    const ObservedSnapshot snap = readSnapshot();
    const bool next = !readToggleValue(id, snap);
    if (id == RowId::ShadowOverride) {
      m_shadowOverrideActive = next;
      pushShadowOverridesToRenderer(snap.pipeline);
      m_dirty = true;
      return;
    }
    if (m_envVars == nullptr) {
      return;
    }
    switch (id) {
      case RowId::HdrEnabled:
        m_envVars->setVar("hdrEnabled", next);
        break;
      case RowId::Fxaa:
        m_envVars->setVar("fxaaEnabled", next);
        break;
      case RowId::BloomEnabled:
        m_envVars->setVar("bloomEnabled", next);
        break;
      case RowId::CanvasBloom:
        m_envVars->setVar("canvasBloomEnabled", next);
        break;
      case RowId::MotionBlurEnabled:
        m_envVars->setVar("motionBlurEnabled", next);
        break;
      case RowId::ShadowsEnabled:
        m_envVars->setVar("shadowsEnabled", next);
        enableShadowOverrideAndPush();
        break;
      case RowId::ShadowPcf:
        m_envVars->setVar("shadowPcf", next);
        enableShadowOverrideAndPush();
        break;
      case RowId::Vsync:
        m_envVars->setVar("vsync", next);
        break;
      case RowId::ShowFps:
        m_envVars->setVar("showFPS", next);
        break;
      default:
        break;
    }
    m_dirty = true;
  }

  int activeChoiceIndex(RowId id, const ObservedSnapshot& snap) const
  {
    switch (id) {
      case RowId::Tonemapper:
        return (snap.pipeline.tonemapper >= 0 && snap.pipeline.tonemapper <= 2)
                 ? snap.pipeline.tonemapper
                 : -1;
      case RowId::TonemapperBright:
        return (snap.pipeline.tonemapper >= 3 && snap.pipeline.tonemapper <= 6)
                 ? (snap.pipeline.tonemapper - 3)
                 : -1;
      case RowId::BloomMips:
        return std::clamp(snap.pipeline.bloomMipCount, 1, 5) - 1;
      case RowId::ShadowCascades:
        return std::clamp(snap.pipeline.shadowCascades, 1, 3) - 1;
      case RowId::ShadowMapSize:
        if (snap.pipeline.shadowMapSize <= 512) {
          return 0;
        }
        if (snap.pipeline.shadowMapSize <= 1024) {
          return 1;
        }
        if (snap.pipeline.shadowMapSize <= 2048) {
          return 2;
        }
        return 3;
      case RowId::FpsCap:
        if (snap.fps <= 0) {
          return 0;
        }
        if (snap.fps == 30) {
          return 1;
        }
        if (snap.fps == 60) {
          return 2;
        }
        if (snap.fps == 120) {
          return 3;
        }
        if (snap.fps == 144) {
          return 4;
        }
        if (snap.fps == 240) {
          return 5;
        }
        return -1;
      default:
        return -1;
    }
  }

  void applyChoice(RowId id, int choiceIndex)
  {
    if (m_envVars == nullptr) {
      return;
    }
    if (id == RowId::Tonemapper) {
      m_envVars->setVar("tonemapper", std::clamp(choiceIndex, 0, 2));
    } else if (id == RowId::TonemapperBright) {
      m_envVars->setVar("tonemapper", std::clamp(choiceIndex, 0, 3) + 3);
    } else if (id == RowId::BloomMips) {
      m_envVars->setVar("bloomMipCount", std::clamp(choiceIndex + 1, 1, 5));
    } else if (id == RowId::ShadowCascades) {
      m_envVars->setVar("shadowCascades", std::clamp(choiceIndex + 1, 1, 3));
    } else if (id == RowId::ShadowMapSize) {
      static constexpr int kSizes[4] = { 512, 1024, 2048, 4096 };
      m_envVars->setVar("shadowMapSize", kSizes[std::clamp(choiceIndex, 0, 3)]);
      enableShadowOverrideAndPush();
    } else if (id == RowId::FpsCap) {
      static constexpr int kCaps[6] = { 0, 30, 60, 120, 144, 240 };
      m_envVars->setVar("fps", kCaps[std::clamp(choiceIndex, 0, 5)]);
    }
    m_dirty = true;
  }

  void sliderRange(RowId id, float* minVal, float* maxVal) const
  {
    switch (id) {
      case RowId::Exposure:
        *minVal = -4.0f;
        *maxVal = 4.0f;
        return;
      case RowId::BloomIntensity:
        *minVal = 0.0f;
        *maxVal = 0.30f;
        return;
      case RowId::MotionBlurAmount:
        *minVal = 0.0f;
        *maxVal = 1.50f;
        return;
      case RowId::MotionBlurMax:
        *minVal = 0.02f;
        *maxVal = 0.50f;
        return;
      case RowId::MotionBlurSamples:
        *minVal = 2.0f;
        *maxVal = 16.0f;
        return;
      case RowId::ShadowBias:
        *minVal = 0.0f;
        *maxVal = 0.010f;
        return;
      case RowId::ShadowSlopeScale:
        *minVal = 0.0f;
        *maxVal = 0.020f;
        return;
      case RowId::ShadowNormalOffset:
        *minVal = 0.0f;
        *maxVal = 0.060f;
        return;
      default:
        *minVal = 0.0f;
        *maxVal = 1.0f;
        return;
    }
  }

  float readSliderFraction(RowId id,
                           const ObservedSnapshot& snap,
                           std::string* formatted) const
  {
    float minVal = 0.0f;
    float maxVal = 1.0f;
    sliderRange(id, &minVal, &maxVal);
    float raw = 0.0f;
    char buf[32] = {};
    switch (id) {
      case RowId::Exposure:
        raw = snap.pipeline.exposure;
        std::snprintf(buf, sizeof(buf), "%+.2f", raw);
        break;
      case RowId::BloomIntensity:
        raw = snap.pipeline.bloomIntensity;
        std::snprintf(buf, sizeof(buf), "%.3f", raw);
        break;
      case RowId::MotionBlurAmount:
        raw = snap.pipeline.motionBlurAmount;
        std::snprintf(buf, sizeof(buf), "%.2f", raw);
        break;
      case RowId::MotionBlurMax:
        raw = snap.pipeline.motionBlurMax;
        std::snprintf(buf, sizeof(buf), "%.2f", raw);
        break;
      case RowId::MotionBlurSamples:
        raw = static_cast<float>(snap.pipeline.motionBlurSamples);
        std::snprintf(buf, sizeof(buf), "%d", snap.pipeline.motionBlurSamples);
        break;
      case RowId::ShadowBias:
        raw = snap.pipeline.shadowBias;
        std::snprintf(buf, sizeof(buf), "%.4f", raw);
        break;
      case RowId::ShadowSlopeScale:
        raw = snap.pipeline.shadowSlopeScale;
        std::snprintf(buf, sizeof(buf), "%.3f", raw);
        break;
      case RowId::ShadowNormalOffset:
        raw = snap.pipeline.shadowNormalOffset;
        std::snprintf(buf, sizeof(buf), "%.3f", raw);
        break;
      default:
        break;
    }
    if (formatted != nullptr) {
      *formatted = buf;
    }
    const float span = std::max(0.000001f, maxVal - minVal);
    return std::clamp((raw - minVal) / span, 0.0f, 1.0f);
  }

  void applySliderFraction(RowId id, float fraction)
  {
    if (m_envVars == nullptr) {
      return;
    }
    float minVal = 0.0f;
    float maxVal = 1.0f;
    sliderRange(id, &minVal, &maxVal);
    const float clamped = std::clamp(fraction, 0.0f, 1.0f);
    const float raw = minVal + (maxVal - minVal) * clamped;

    if (id == RowId::MotionBlurSamples) {
      const int samples = std::clamp(static_cast<int>(std::lround(raw)), 2, 16);
      m_envVars->setVar("motionBlurSamples", samples);
      m_dirty = true;
      return;
    }

    const float quantized =
      (id == RowId::Exposure || id == RowId::MotionBlurAmount ||
       id == RowId::MotionBlurMax)
        ? (std::round(raw * 100.0f) / 100.0f)
        : (std::round(raw * 10000.0f) / 10000.0f);
    const std::string text = RenderPipelineConfig::formatFloat(quantized);

    switch (id) {
      case RowId::Exposure:
        m_envVars->setVar("exposure", text);
        break;
      case RowId::BloomIntensity:
        m_envVars->setVar("bloomIntensity", text);
        break;
      case RowId::MotionBlurAmount:
        m_envVars->setVar("motionBlurAmount", text);
        break;
      case RowId::MotionBlurMax:
        m_envVars->setVar("motionBlurMax", text);
        break;
      case RowId::ShadowBias:
        m_envVars->setVar("shadowBias", text);
        enableShadowOverrideAndPush();
        break;
      case RowId::ShadowSlopeScale:
        m_envVars->setVar("shadowSlopeScale", text);
        enableShadowOverrideAndPush();
        break;
      case RowId::ShadowNormalOffset:
        m_envVars->setVar("shadowNormalOffset", text);
        enableShadowOverrideAndPush();
        break;
      default:
        break;
    }
    m_dirty = true;
  }

  void enableShadowOverrideAndPush()
  {
    m_shadowOverrideActive = true;
    pushShadowOverridesToRenderer(RenderPipelineConfig::fromEnv(m_envVars));
  }

  bool handleScroll(double offset)
  {
    if (!needsScrollbar() || offset == 0.0) {
      return false;
    }
    const int delta = offset > 0.0 ? -2 : 2;
    const int next = std::clamp(m_firstRow + delta, 0, maxFirstRow());
    if (next != m_firstRow) {
      m_firstRow = next;
      m_dirty = true;
    }
    return true;
  }

  void scrubScrollbar(float mouseY)
  {
    const GuiToolRect body = bodyRect(panelRect());
    const GuiToolRect track = scrollbarTrackRect(body);
    const float t =
      std::clamp((mouseY - track.y) / std::max(1.0f, track.h), 0.0f, 1.0f);
    const int next =
      static_cast<int>(std::lround(t * static_cast<float>(maxFirstRow())));
    if (next != m_firstRow) {
      m_firstRow = next;
      m_dirty = true;
    }
  }

  void scrubSliderAt(int rowIndex, float mouseX)
  {
    if (rowIndex < m_firstRow || rowIndex >= kRowCount) {
      return;
    }
    const GuiToolRect body = bodyRect(panelRect());
    const GuiToolRect rowRect = rowContentRect(body, rowIndex - m_firstRow);
    const GuiToolRect track = GuiToolStyle::sliderTrack(rowRect);
    const float fraction = (mouseX - track.x) / std::max(1.0f, track.w);
    applySliderFraction(static_cast<RowId>(rowIndex), fraction);
  }

  void handlePointerPress(float mx, float my)
  {
    const GuiToolRect panel = panelRect();
    const GuiToolRect bar = titleBarRect(panel);
    if (bar.contains(mx, my)) {
      const std::vector<GuiToolTitleButton> buttons = titleButtons();
      for (std::size_t i = 0; i < buttons.size(); ++i) {
        const GuiToolRect btn =
          GuiToolStyle::titleButtonRect(bar, static_cast<int>(i));
        if (btn.contains(mx, my)) {
          if (buttons[i] == GuiToolTitleButton::Hide) {
            if (m_detached) {
              m_pendingRequest =
                WindowRequest{ WindowRequestKind::Close, 0, 0, 0, 0 };
            } else {
              setOpen(false);
            }
          } else if (buttons[i] == GuiToolTitleButton::PopOut) {
            const float scale =
              m_renderer != nullptr ? m_renderer->getUiScale() : 1.0f;
            requestDetach(scale);
          } else if (buttons[i] == GuiToolTitleButton::Dock) {
            requestDock();
          }
          return;
        }
      }
      if (!m_detached) {
        m_dragMode = DragMode::MovePanel;
        m_dragOffsetX = mx - m_x;
        m_dragOffsetY = my - m_y;
      }
      return;
    }

    if (!m_detached && resizeGripRect(panel).contains(mx, my)) {
      m_dragMode = DragMode::ResizePanel;
      m_dragStartW = m_width;
      m_dragStartH = m_height;
      m_dragStartX = mx;
      m_dragStartY = my;
      return;
    }

    const GuiToolRect footer = footerBarRect(panel);
    if (footer.contains(mx, my)) {
      if (footerButtonRect(footer, 0).contains(mx, my)) {
        resetDefaults();
      } else if (footerButtonRect(footer, 1).contains(mx, my)) {
        if (m_envVars != nullptr) {
          m_envVars->save();
          setStatus("Saved envvars.json");
        }
      }
      return;
    }

    const GuiToolRect body = bodyRect(panel);
    if (!body.contains(mx, my)) {
      return;
    }
    if (needsScrollbar()) {
      const GuiToolRect track = scrollbarTrackRect(body);
      if (mx >= track.x - 2.0f) {
        m_dragMode = DragMode::Scrollbar;
        scrubScrollbar(my);
        return;
      }
    }

    const int slot = static_cast<int>(std::floor((my - body.y) / kRowHeight));
    const int rowIndex = m_firstRow + slot;
    if (slot < 0 || slot >= visibleRowCount() || rowIndex >= kRowCount) {
      return;
    }
    const RowId id = static_cast<RowId>(rowIndex);
    const RowKind kind = rowKind(id);
    const GuiToolRect rowRect = rowContentRect(body, slot);
    if (kind == RowKind::Toggle) {
      applyToggle(id);
    } else if (kind == RowKind::Choice) {
      const int count = choiceCount(id);
      for (int c = 0; c < count; ++c) {
        if (choiceButtonRect(rowRect, count, c).contains(mx, my)) {
          applyChoice(id, c);
          return;
        }
      }
    } else if (kind == RowKind::Slider) {
      const GuiToolRect track = GuiToolStyle::sliderTrack(rowRect);
      if (mx >= track.x - 8.0f) {
        m_dragMode = DragMode::Slider;
        m_activeRow = rowIndex;
        scrubSliderAt(rowIndex, mx);
      }
    }
  }

  void handlePointerDrag(float mx, float my)
  {
    if (m_dragMode == DragMode::MovePanel) {
      m_x = mx - m_dragOffsetX;
      m_y = my - m_dragOffsetY;
      clampPanelToViewport();
      m_dirty = true;
    } else if (m_dragMode == DragMode::ResizePanel) {
      m_width = std::max(kMinWidth, m_dragStartW + (mx - m_dragStartX));
      m_height = std::max(kMinHeight, m_dragStartH + (my - m_dragStartY));
      clampPanelToViewport();
      m_dirty = true;
    } else if (m_dragMode == DragMode::Scrollbar) {
      scrubScrollbar(my);
    } else if (m_dragMode == DragMode::Slider && m_activeRow >= 0) {
      scrubSliderAt(m_activeRow, mx);
    }
  }

  void clearHover()
  {
    if (m_hoverTitleButton != -1 || m_hoverRow != -1 || m_hoverChoice != -1 ||
        m_hoverFooterButton != -1) {
      m_hoverTitleButton = -1;
      m_hoverRow = -1;
      m_hoverChoice = -1;
      m_hoverFooterButton = -1;
      m_dirty = true;
    }
  }

  void updateHover(float mx, float my)
  {
    const GuiToolRect panel = panelRect();
    if (!panel.contains(mx, my)) {
      clearHover();
      return;
    }
    int nextTitle = -1;
    int nextRow = -1;
    int nextChoice = -1;
    int nextFooter = -1;

    const GuiToolRect bar = titleBarRect(panel);
    if (bar.contains(mx, my)) {
      const std::vector<GuiToolTitleButton> buttons = titleButtons();
      for (std::size_t i = 0; i < buttons.size(); ++i) {
        if (GuiToolStyle::titleButtonRect(bar, static_cast<int>(i))
              .contains(mx, my)) {
          nextTitle = static_cast<int>(i);
          break;
        }
      }
    } else {
      const GuiToolRect footer = footerBarRect(panel);
      if (footer.contains(mx, my)) {
        for (int b = 0; b < 2; ++b) {
          if (footerButtonRect(footer, b).contains(mx, my)) {
            nextFooter = b;
            break;
          }
        }
      } else {
        const GuiToolRect body = bodyRect(panel);
        if (body.contains(mx, my)) {
          const int slot =
            static_cast<int>(std::floor((my - body.y) / kRowHeight));
          const int rowIndex = m_firstRow + slot;
          if (slot >= 0 && slot < visibleRowCount() && rowIndex < kRowCount) {
            const RowId id = static_cast<RowId>(rowIndex);
            if (rowKind(id) != RowKind::Section) {
              nextRow = rowIndex;
              if (rowKind(id) == RowKind::Choice) {
                const GuiToolRect rowRect = rowContentRect(body, slot);
                const int count = choiceCount(id);
                for (int c = 0; c < count; ++c) {
                  if (choiceButtonRect(rowRect, count, c).contains(mx, my)) {
                    nextChoice = c;
                    break;
                  }
                }
              }
            }
          }
        }
      }
    }

    if (nextTitle != m_hoverTitleButton || nextRow != m_hoverRow ||
        nextChoice != m_hoverChoice || nextFooter != m_hoverFooterButton) {
      m_hoverTitleButton = nextTitle;
      m_hoverRow = nextRow;
      m_hoverChoice = nextChoice;
      m_hoverFooterButton = nextFooter;
      m_dirty = true;
    }
  }

  void compose()
  {
    m_visual.clearPrimitives();
    const GuiToolRect panel = panelRect();
    GuiToolStyle::panel(m_visual, panel);

    std::string title = "Graphics Settings (F7)";
    if (m_window != nullptr) {
      const std::string api = m_window->graphicsApi();
      if (!api.empty()) {
        title += " - " + api;
      }
    }
    GuiToolStyle::titleBar(
      m_visual, titleBarRect(panel), title, titleButtons(), m_hoverTitleButton);

    const ObservedSnapshot snap = readSnapshot();
    const GuiToolRect body = bodyRect(panel);
    const int visRows = visibleRowCount();
    for (int slot = 0; slot < visRows; ++slot) {
      const int rowIndex = m_firstRow + slot;
      if (rowIndex >= kRowCount) {
        break;
      }
      const RowId id = static_cast<RowId>(rowIndex);
      const RowKind kind = rowKind(id);
      const GuiToolRect rowRect = rowContentRect(body, slot);
      const bool hovered = (m_hoverRow == rowIndex);

      if (kind == RowKind::Section) {
        GuiToolStyle::sectionHeader(m_visual, rowRect, rowLabel(id));
      } else if (kind == RowKind::Toggle) {
        GuiToolStyle::toggle(
          m_visual, rowRect, rowLabel(id), readToggleValue(id, snap), hovered);
      } else if (kind == RowKind::Slider) {
        std::string valueText;
        const float frac = readSliderFraction(id, snap, &valueText);
        const bool active =
          (m_dragMode == DragMode::Slider && m_activeRow == rowIndex);
        GuiToolStyle::slider(
          m_visual, rowRect, rowLabel(id), frac, valueText, hovered, active);
      } else if (kind == RowKind::Choice) {
        const float labelWidth = std::min(108.0f, rowRect.w * 0.34f);
        GuiToolStyle::fittedText(
          m_visual,
          rowLabel(id),
          rowRect.x + GuiToolStyle::kPad,
          rowRect.y +
            std::max(0.0f,
                     std::round((rowRect.h - GuiToolStyle::kFontSize) * 0.5f)) -
            1.0f,
          labelWidth - 4.0f,
          GuiToolStyle::kFontSize,
          GuiToolPalette::text);
        const int count = choiceCount(id);
        const int selected = activeChoiceIndex(id, snap);
        for (int c = 0; c < count; ++c) {
          const GuiToolRect btnRect = choiceButtonRect(rowRect, count, c);
          GuiToolStyle::button(m_visual,
                               btnRect,
                               choiceLabel(id, c),
                               hovered && m_hoverChoice == c,
                               selected == c);
        }
      }
    }

    if (needsScrollbar()) {
      GuiToolStyle::scrollbar(m_visual,
                              scrollbarTrackRect(body),
                              static_cast<float>(m_firstRow),
                              static_cast<float>(visRows),
                              static_cast<float>(kRowCount));
    }

    const GuiToolRect footer = footerBarRect(panel);
    m_visual.addFilledRect(footer.x + 1.0f,
                           footer.y,
                           footer.w - 2.0f,
                           footer.h - 1.0f,
                           GuiToolPalette::bar);
    m_visual.addLine(footer.x + 1.0f,
                     footer.y + 0.5f,
                     footer.x + footer.w - 1.0f,
                     footer.y + 0.5f,
                     GuiToolPalette::rule,
                     1.0f);
    GuiToolStyle::button(m_visual,
                         footerButtonRect(footer, 0),
                         "Reset Defaults",
                         m_hoverFooterButton == 0,
                         false);
    GuiToolStyle::button(m_visual,
                         footerButtonRect(footer, 1),
                         "Save",
                         m_hoverFooterButton == 1,
                         false);

    const std::string rightNote =
      !m_statusText.empty()
        ? m_statusText
        : (m_shadowOverrideActive ? std::string("Shadow override ON")
                                  : std::string("Live"));
    const float noteX = footer.x + GuiToolStyle::kPad + 206.0f;
    const float noteMaxW =
      footer.x + footer.w - noteX - (m_detached ? 8.0f : kGripSize + 4.0f);
    if (noteMaxW > 20.0f) {
      GuiToolStyle::fittedText(
        m_visual,
        rightNote,
        noteX,
        footer.y +
          std::max(
            0.0f,
            std::round((footer.h - GuiToolStyle::kSmallFontSize) * 0.5f)) -
          1.0f,
        noteMaxW,
        GuiToolStyle::kSmallFontSize,
        !m_statusText.empty() ? GuiToolPalette::good
                              : (m_shadowOverrideActive ? GuiToolPalette::accent
                                                        : GuiToolPalette::dim));
    }

    if (!m_detached) {
      // Diagonal resize grip lines in the bottom-right corner.
      const float rx = panel.x + panel.w - 3.0f;
      const float ry = panel.y + panel.h - 3.0f;
      m_visual.addLine(rx - 8.0f, ry, rx, ry - 8.0f, GuiToolPalette::dim, 1.0f);
      m_visual.addLine(rx - 4.0f, ry, rx, ry - 4.0f, GuiToolPalette::dim, 1.0f);
    }
  }
};
