#pragma once

#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/DrawList.h>
#include <Illumo/Rendering/Font.h>
#include <Illumo/Rendering/GLString.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/Gamepad.h>
#include <Illumo/Services/IEnvVars.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Human-readable display name for a GamepadButton.
inline const char*
gamepadButtonName(GamepadButton button)
{
  switch (button) {
    case GamepadButton::A:
      return "A";
    case GamepadButton::B:
      return "B";
    case GamepadButton::X:
      return "X";
    case GamepadButton::Y:
      return "Y";
    case GamepadButton::LeftBumper:
      return "Left Bumper";
    case GamepadButton::RightBumper:
      return "Right Bumper";
    case GamepadButton::Back:
      return "Back";
    case GamepadButton::Start:
      return "Start";
    case GamepadButton::Guide:
      return "Guide";
    case GamepadButton::LeftThumb:
      return "Left Thumb";
    case GamepadButton::RightThumb:
      return "Right Thumb";
    case GamepadButton::DpadUp:
      return "Dpad Up";
    case GamepadButton::DpadRight:
      return "Dpad Right";
    case GamepadButton::DpadDown:
      return "Dpad Down";
    case GamepadButton::DpadLeft:
      return "Dpad Left";
    case GamepadButton::Count:
    default:
      return "Unknown";
  }
}

// Normalizes a trigger reading from either [-1.0, 1.0] or [0.0, 1.0] to
// [0.0, 1.0].
inline float
normalizeGamepadTrigger(float value)
{
  if (value < 0.0f) {
    const float mapped = (value + 1.0f) * 0.5f;
    return mapped < 0.0f ? 0.0f : (mapped > 1.0f ? 1.0f : mapped);
  }
  return value > 1.0f ? 1.0f : value;
}

// Detects newly pressed gamepad buttons and triggers, dispatching console and
// log notifications.
class GamepadDebugNotifier
{
public:
  std::vector<GamepadButton> poll(InputManager* input)
  {
    std::vector<GamepadButton> pressed;
    if (input == nullptr) {
      m_buttonDown.fill(false);
      m_leftTrigger = 0.0f;
      m_rightTrigger = 0.0f;
      m_leftTriggerDown = false;
      m_rightTriggerDown = false;
      m_connected = false;
      return pressed;
    }

    m_connected = input->isGamepadConnected();
    m_initialized = true;

    for (std::size_t i = 0; i < static_cast<std::size_t>(GamepadButton::Count);
         ++i) {
      const auto button = static_cast<GamepadButton>(i);
      const bool down = input->isGamepadButtonPressed(button);
      const bool justPressed =
        input->isGamepadButtonJustPressed(button) || (down && !m_buttonDown[i]);
      m_buttonDown[i] = down;
      if (justPressed) {
        pressed.push_back(button);
        m_lastPressedButton = gamepadButtonName(button);
        ++m_pressCount;
      }
    }

    return pressed;
  }

  int pollAndNotify(InputManager* input, CommandLine* commandLine)
  {
    const bool wasConnected = m_connected;
    const std::vector<GamepadButton> pressed = poll(input);

    if (m_initialized && m_connected != wasConnected) {
      const std::string connMsg =
        m_connected ? "Gamepad connected: controller is ready"
                    : "Gamepad disconnected: controller removed";
      if (commandLine != nullptr) {
        commandLine->AppendStringLn(150, 220, 255, 255, connMsg);
      }
      Logger::LogInfo(connMsg);
    }

    for (GamepadButton button : pressed) {
      const std::string name = gamepadButtonName(button);
      const std::string notification = "Gamepad button pressed: " + name;
      if (commandLine != nullptr) {
        commandLine->AppendStringLn(200, 240, 255, 255, notification);
      }
      Logger::LogInfo(notification);
    }

    int notifiedCount = static_cast<int>(pressed.size());

    if (input != nullptr && m_connected) {
      m_leftTrigger = input->getGamepadLeftTrigger();
      m_rightTrigger = input->getGamepadRightTrigger();

      const float normLt = normalizeGamepadTrigger(m_leftTrigger);
      const float normRt = normalizeGamepadTrigger(m_rightTrigger);

      // Hysteresis thresholds for trigger button edge detection:
      // When squeezed past 40%, report a press event.
      // Must be released below 15% before another press triggers.
      static constexpr float kPressThreshold = 0.40f;
      static constexpr float kReleaseThreshold = 0.15f;

      auto checkTrigger =
        [&](float normalized, bool& isDownRef, const char* name) {
          if (normalized >= kPressThreshold) {
            if (!isDownRef) {
              isDownRef = true;
              m_lastPressedButton = name;
              ++m_pressCount;
              ++notifiedCount;
              const std::string notification =
                std::string("Gamepad button pressed: ") + name;
              if (commandLine != nullptr) {
                commandLine->AppendStringLn(200, 240, 255, 255, notification);
              }
              Logger::LogInfo(notification);
            }
          } else if (normalized <= kReleaseThreshold) {
            isDownRef = false;
          }
        };

      checkTrigger(normLt, m_leftTriggerDown, "Left Trigger");
      checkTrigger(normRt, m_rightTriggerDown, "Right Trigger");
    }

    return notifiedCount;
  }

  bool isDown(GamepadButton button) const
  {
    const auto index = static_cast<std::size_t>(button);
    return index < m_buttonDown.size() && m_buttonDown[index];
  }
  bool isLeftTriggerDown() const { return m_leftTriggerDown; }
  bool isRightTriggerDown() const { return m_rightTriggerDown; }
  float leftTrigger() const { return m_leftTrigger; }
  float rightTrigger() const { return m_rightTrigger; }

  bool isConnected() const { return m_connected; }
  const std::string& lastPressedButton() const { return m_lastPressedButton; }
  std::uint32_t pressCount() const { return m_pressCount; }

private:
  std::array<bool, static_cast<std::size_t>(GamepadButton::Count)>
    m_buttonDown{};
  float m_leftTrigger = 0.0f;
  float m_rightTrigger = 0.0f;
  bool m_leftTriggerDown = false;
  bool m_rightTriggerDown = false;
  std::string m_lastPressedButton;
  std::uint32_t m_pressCount = 0;
  bool m_connected = false;
  bool m_initialized = false;
};

// Visual 2D debug widget displaying interactive joystick positions and click
// flash states.
class JoystickDebugVisual
{
public:
  JoystickDebugVisual()
  {
    m_visual.setSpace(PrimitiveSpace::Pixels);
    m_visual.setLayerHint(RenderLayerId::Debug);
  }
  ~JoystickDebugVisual() = default;

  void init(Renderer* renderer, IRenderWindow* window, Camera* camera = nullptr)
  {
    m_visual.setWindow(window);
    m_visual.setCamera(camera);
    m_visual.setRenderer(renderer);
    m_visual.setSpace(PrimitiveSpace::Pixels);
    m_visual.setLayerHint(RenderLayerId::Debug);
    if (renderer != nullptr) {
      m_visual.prepare(renderer);
    }
  }

  void update(InputManager* input, double elapsed)
  {
    if (input == nullptr || !input->isGamepadConnected()) {
      m_connected = false;
      m_leftPos = { 0.0f, 0.0f };
      m_rightPos = { 0.0f, 0.0f };
      m_leftClicked = false;
      m_rightClicked = false;
      m_leftFlashTimer = 0.0f;
      m_rightFlashTimer = 0.0f;
      rebuildShapes();
      return;
    }

    m_connected = true;

    const std::array<float, 2> ls = input->getGamepadLeftStick();
    const std::array<float, 2> rs = input->getGamepadRightStick();

    auto clampStick = [](float x, float y) -> std::array<float, 2> {
      const float len = std::sqrt(x * x + y * y);
      if (len > 1.0f && len > 0.0001f) {
        return { x / len, y / len };
      }
      return { x, y };
    };

    m_leftPos = clampStick(ls[0], ls[1]);
    m_rightPos = clampStick(rs[0], rs[1]);

    const bool l3Down = input->isGamepadButtonPressed(GamepadButton::LeftThumb);
    const bool r3Down =
      input->isGamepadButtonPressed(GamepadButton::RightThumb);

    static constexpr float kMaxFlashDuration = 0.35f;

    if (l3Down) {
      m_leftFlashTimer = kMaxFlashDuration;
      m_leftClicked = true;
    } else {
      m_leftClicked = false;
      if (m_leftFlashTimer > 0.0f) {
        m_leftFlashTimer =
          std::max(0.0f, m_leftFlashTimer - static_cast<float>(elapsed));
      }
    }

    if (r3Down) {
      m_rightFlashTimer = kMaxFlashDuration;
      m_rightClicked = true;
    } else {
      m_rightClicked = false;
      if (m_rightFlashTimer > 0.0f) {
        m_rightFlashTimer =
          std::max(0.0f, m_rightFlashTimer - static_cast<float>(elapsed));
      }
    }

    rebuildShapes();
  }

  void prepare(Renderer* renderer)
  {
    if (m_connected && renderer != nullptr) {
      m_visual.prepare(renderer);
    }
  }

  void draw(DrawList& frame)
  {
    if (m_connected) {
      frame.AddDrawable(&m_visual, RenderLayerId::Debug);
    }
  }

  float leftFlash() const { return m_leftFlashTimer; }
  float rightFlash() const { return m_rightFlashTimer; }
  bool isLeftClicked() const { return m_leftClicked; }
  bool isRightClicked() const { return m_rightClicked; }
  const std::array<float, 2>& leftPosition() const { return m_leftPos; }
  const std::array<float, 2>& rightPosition() const { return m_rightPos; }
  bool isConnected() const { return m_connected; }
  std::size_t shapeCount() const { return m_visual.shapeCount(); }
  GameVisual& visual() { return m_visual; }

  void setLeftCenter(float x, float y) { m_leftCenter = { x, y }; }
  void setRightCenter(float x, float y) { m_rightCenter = { x, y }; }
  void setRadius(float r) { m_socketRadius = r; }
  const std::array<float, 2>& leftCenter() const { return m_leftCenter; }
  const std::array<float, 2>& rightCenter() const { return m_rightCenter; }
  float radius() const { return m_socketRadius; }
  void rebuild() { rebuildShapes(); }

private:
  GameVisual m_visual;
  std::array<float, 2> m_leftCenter{ 40.0f, 60.0f };
  std::array<float, 2> m_rightCenter{ 116.0f, 60.0f };
  float m_socketRadius = 24.0f;
  float m_stickDotRadius = 8.0f;

  std::array<float, 2> m_leftPos{ 0.0f, 0.0f };
  std::array<float, 2> m_rightPos{ 0.0f, 0.0f };
  float m_leftFlashTimer = 0.0f;
  float m_rightFlashTimer = 0.0f;
  bool m_leftClicked = false;
  bool m_rightClicked = false;
  bool m_connected = false;

  void rebuildShapes()
  {
    m_visual.clearPrimitives();
    if (!m_connected) {
      return;
    }

    static constexpr float kMaxFlashDuration = 0.35f;

    auto drawStick = [&](const std::array<float, 2>& center,
                         const std::array<float, 2>& pos,
                         float flashTimer,
                         bool clicked,
                         const char* label) {
      const float cx = center[0];
      const float cy = center[1];
      const float R = m_socketRadius;
      const float r = m_stickDotRadius;
      const float maxDeflection = R - r - 2.0f;

      const float flash =
        std::clamp(flashTimer / kMaxFlashDuration, 0.0f, 1.0f);

      auto lerpChannel =
        [](unsigned char a, unsigned char b, float t) -> unsigned char {
        return static_cast<unsigned char>(a + (b - a) * t + 0.5f);
      };

      ColorRgba rimColor;
      rimColor.r = lerpChannel(65, 255, flash);
      rimColor.g = lerpChannel(85, 240, flash);
      rimColor.b = lerpChannel(120, 90, flash);
      rimColor.a = 230;

      ColorRgba socketBg{ 20, 26, 36, 210 };
      if (flash > 0.0f) {
        socketBg.r = lerpChannel(20, 80, flash);
        socketBg.g = lerpChannel(26, 75, flash);
        socketBg.b = lerpChannel(36, 30, flash);
      }

      // 1. Socket rim border
      m_visual.addFilledEllipse(cx - R, cy - R, R * 2.0f, R * 2.0f, rimColor);

      // 2. Socket inner background
      const float innerR = R - 2.0f;
      m_visual.addFilledEllipse(
        cx - innerR, cy - innerR, innerR * 2.0f, innerR * 2.0f, socketBg);

      // 3. Subtle center crosshair
      ColorRgba crossColor{ 50, 65, 88, 140 };
      m_visual.addLine(cx - 4.0f, cy, cx + 4.0f, cy, crossColor, 1.0f);
      m_visual.addLine(cx, cy - 4.0f, cx, cy + 4.0f, crossColor, 1.0f);

      // 4. Deflection vector line from center to dot
      const float dotX = cx + pos[0] * maxDeflection;
      const float dotY = cy + pos[1] * maxDeflection;
      const float distSq = pos[0] * pos[0] + pos[1] * pos[1];
      if (distSq > 0.01f) {
        ColorRgba trailColor{ 70, 150, 220, 160 };
        if (flash > 0.0f) {
          trailColor.r = lerpChannel(70, 255, flash);
          trailColor.g = lerpChannel(150, 240, flash);
          trailColor.b = lerpChannel(220, 120, flash);
        }
        m_visual.addLine(cx, cy, dotX, dotY, trailColor, 1.5f);
      }

      // 5. Thumbstick dot / cap
      const float currentR = r + 2.0f * flash;
      ColorRgba dotRimColor{ 255, 255, 255, 210 };
      ColorRgba dotCoreColor;
      dotCoreColor.r = lerpChannel(70, 255, flash);
      dotCoreColor.g = lerpChannel(200, 255, flash);
      dotCoreColor.b = lerpChannel(255, 140, flash);
      dotCoreColor.a = 255;

      m_visual.addFilledEllipse(dotX - (currentR + 1.5f),
                                dotY - (currentR + 1.5f),
                                (currentR + 1.5f) * 2.0f,
                                (currentR + 1.5f) * 2.0f,
                                dotRimColor);
      m_visual.addFilledEllipse(dotX - currentR,
                                dotY - currentR,
                                currentR * 2.0f,
                                currentR * 2.0f,
                                dotCoreColor);

      // 6. Label below circle
      ColorRgba labelColor;
      labelColor.r = lerpChannel(180, 255, flash);
      labelColor.g = lerpChannel(200, 250, flash);
      labelColor.b = lerpChannel(230, 100, flash);
      labelColor.a = 230;
      std::string text = label;
      if (clicked || flash > 0.5f) {
        text += " *";
      }
      m_visual.addText(text, cx - 8.0f, cy + R + 3.0f, 10.0f, labelColor);
    };

    drawStick(m_leftCenter, m_leftPos, m_leftFlashTimer, m_leftClicked, "LS");
    drawStick(
      m_rightCenter, m_rightPos, m_rightFlashTimer, m_rightClicked, "RS");
  }
};

// DebugOverlay's gamepad and joystick HUD (the `gamepad` / `joystick_debug`
// commands and `showGamepadVisuals` environment variable).
class GamepadOverlay
{
public:
  GamepadOverlay() = default;
  GamepadOverlay(const GamepadOverlay&) = delete;
  GamepadOverlay& operator=(const GamepadOverlay&) = delete;
  GamepadOverlay(GamepadOverlay&&) = delete;
  GamepadOverlay& operator=(GamepadOverlay&&) = delete;
  ~GamepadOverlay() = default;

  static bool isEnabled(IEnvVars* environment)
  {
    if (environment == nullptr) {
      return false;
    }
    const EnvVar& primary = environment->getVar("showGamepadVisuals");
    if (!primary.value.empty()) {
      return primary.valueAsBool;
    }
    const EnvVar& alt = environment->getVar("testGamepad");
    if (!alt.value.empty()) {
      return alt.valueAsBool;
    }
    return false;
  }

  void prepare(Renderer* renderer, IRenderWindow* window, Camera* camera)
  {
    m_renderer = renderer;
    m_statusText.setRenderer(renderer);
    m_statusText.setSize(18);
    m_statusText.setX(static_cast<int>(m_left));
    m_statusText.setY(static_cast<int>(m_top));
    m_statusText.setR(255);
    m_statusText.setG(255);
    m_statusText.setB(255);
    m_statusText.setA(255);
    m_statusText.setGlyphBacking(true);
    m_statusText.setContent("Gamepad: Initializing...");
    m_statusText.setVisible(false);
    m_joystickVisual.init(renderer, window, camera);
    layout(m_left, m_top, Font::getDefaultFont().get());
  }

  void update(double dt,
              IEnvVars* environment,
              InputManager* input,
              CommandLine* commandLine)
  {
    m_enabled = isEnabled(environment);
    m_statusText.setVisible(m_enabled);
    if (!m_enabled) {
      return;
    }

    m_notifier.pollAndNotify(input, commandLine);

    std::string status;
    if (input != nullptr && input->isGamepadConnected()) {
      status = "Gamepad: [Connected]";
      if (!m_notifier.lastPressedButton().empty()) {
        status += "  Last button: [" + m_notifier.lastPressedButton() +
                  "] (total: " + std::to_string(m_notifier.pressCount()) + ")";
      } else {
        status += "  Press any button/trigger to test";
      }

      const float normLt = normalizeGamepadTrigger(m_notifier.leftTrigger());
      const float normRt = normalizeGamepadTrigger(m_notifier.rightTrigger());
      auto formatPct = [](float v) -> std::string {
        const int pct = static_cast<int>(v * 100.0f + 0.5f);
        return std::to_string(pct) + "%";
      };
      status += "  LT: " + formatPct(normLt) + "  RT: " + formatPct(normRt);

      m_statusText.setR(100);
      m_statusText.setG(255);
      m_statusText.setB(100);
    } else {
      status = "Gamepad: [Not Detected] (connect controller)";
      m_statusText.setR(255);
      m_statusText.setG(180);
      m_statusText.setB(80);
    }
    m_statusText.setContent(std::move(status));

    m_joystickVisual.update(input, dt);
    layout(m_left, m_top, Font::getDefaultFont().get());
  }

  // Positions the status label at (left, top) and places connected joystick
  // sockets beneath it, reporting the bounding width and bottom Y coordinate.
  void layout(float left,
              float top,
              const Font* font,
              float* outWidth = nullptr,
              float* outBottom = nullptr)
  {
    m_left = left;
    m_top = top;
    m_statusText.setX(static_cast<int>(left));
    m_statusText.setY(static_cast<int>(top));

    const float sizePt = static_cast<float>(m_statusText.getSize());
    float textWidth =
      static_cast<float>(m_statusText.getContent().size()) * sizePt * 0.55f;
    float textHeight = sizePt * 1.25f;
    if (font != nullptr && !m_statusText.getContent().empty()) {
      const TextBounds bounds =
        font->measureText(m_statusText.getContent(), sizePt);
      if (bounds.width > 0.0f) {
        textWidth = bounds.width;
      }
      if (bounds.height > 0.0f) {
        textHeight = bounds.height;
      }
    }

    const float R = m_joystickVisual.radius();
    const float stickGap = 8.0f;
    const float centerY = top + textHeight + stickGap + R;
    const float leftCenterX = left + R + 4.0f;
    const float rightCenterX = leftCenterX + R * 2.0f + 28.0f;
    m_joystickVisual.setLeftCenter(leftCenterX, centerY);
    m_joystickVisual.setRightCenter(rightCenterX, centerY);
    if (m_enabled && m_joystickVisual.isConnected()) {
      m_joystickVisual.rebuild();
      m_joystickVisual.prepare(m_renderer);
    }

    float width = textWidth;
    float bottom = top + textHeight;
    if (m_joystickVisual.isConnected()) {
      const float sticksWidth = (rightCenterX + R) - left;
      if (sticksWidth > width) {
        width = sticksWidth;
      }
      // Include the "LS" / "RS" label below the socket rim.
      bottom = centerY + R + 15.0f;
    }
    if (outWidth != nullptr) {
      *outWidth = width;
    }
    if (outBottom != nullptr) {
      *outBottom = bottom;
    }
  }

  void dispatch(DrawList& scene)
  {
    if (!m_enabled) {
      return;
    }
    m_joystickVisual.draw(scene);
    scene.AddDrawable(&m_statusText, RenderLayerId::Debug);
  }

  bool visible() const { return m_enabled; }
  float left() const { return m_left; }
  float top() const { return m_top; }
  GLString& statusLabel() { return m_statusText; }
  const GLString& statusLabel() const { return m_statusText; }
  JoystickDebugVisual& joystickVisual() { return m_joystickVisual; }
  const JoystickDebugVisual& joystickVisual() const { return m_joystickVisual; }
  GamepadDebugNotifier& notifier() { return m_notifier; }
  const GamepadDebugNotifier& notifier() const { return m_notifier; }

private:
  Renderer* m_renderer = nullptr;
  GamepadDebugNotifier m_notifier;
  JoystickDebugVisual m_joystickVisual;
  GLString m_statusText;
  float m_left = 12.0f;
  float m_top = 12.0f;
  bool m_enabled = false;
};
