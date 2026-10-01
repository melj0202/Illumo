#pragma once

#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/ResourceHandle.h>
#include <array>
#include <string>

class AssetManager;
class DrawList;
class InputManager;
class IRenderWindow;
class Renderer;

// The Illumo engine's own brand (docs/brand): the opening splash the runtime
// plays before every app, and a "Powered by" badge products may draw. Their
// images ship with the engine assets in Illumo/Assets/Branding (kAssetName,
// beside the runtime), which a guest reads as /engine/Branding (kGuestPath).
// When an image is missing the splash finishes at once and the badge draws
// nothing, so nothing depends on them.

// The badge: a small "POWERED BY" label over the flat wordmark, drawn into
// the caller's visual. It holds only its image.
class GuiEngineBadge final
{
public:
  static constexpr const char* kAssetName = "Branding/illumo-badge.png";
  static constexpr const char* kGuestPath = "/engine/Branding/illumo-badge.png";

  GuiEngineBadge() = default;
  // Releases the image through the AssetManager that acquired it.
  ~GuiEngineBadge();
  GuiEngineBadge(const GuiEngineBadge&) = delete;
  GuiEngineBadge& operator=(const GuiEngineBadge&) = delete;
  GuiEngineBadge(GuiEngineBadge&&) = delete;
  GuiEngineBadge& operator=(GuiEngineBadge&&) = delete;

  // Acquires the wordmark from `path`. False, and nothing to draw, when
  // `assets` is null or the image is unavailable.
  bool acquire(AssetManager* assets, const std::string& path = kGuestPath);
  // Idempotent.
  void release();
  bool ready() const { return m_image.isValid() && m_aspect > 0.0f; }

  // The badge's width for a wordmark `height` tall (0 when not ready).
  float width(float height) const;
  // Draws the badge with its lower left corner at (left, bottom); the label
  // sits above the wordmark. Nothing when not ready.
  void draw(GameVisual& visual,
            float left,
            float bottom,
            float height,
            unsigned char opacity) const;

private:
  AssetManager* m_assets = nullptr;
  TextureHandle m_image{};
  // Width over height of the wordmark image.
  float m_aspect = 0.0f;
};

// The opening splash: over the brand's warm dark ground, the neon logo
// strikes like a tube lighting (a plain fade with reduced motion), holds,
// and fades out as one piece. Any key press, except the console's Grave
// key, or a fresh click skips it. It owns its layers; its owner
// (RuntimeShell) dispatches them and moves on once finished().
class GuiEngineSplash final
{
public:
  static constexpr const char* kAssetName = "Branding/illumo-splash.png";
  static constexpr float kLightSeconds = 0.7f;
  static constexpr float kHoldSeconds = 1.1f;
  static constexpr float kFadeSeconds = 0.5f;
  static constexpr float kTotalSeconds =
    kLightSeconds + kHoldSeconds + kFadeSeconds;

  GuiEngineSplash();
  // Releases the logo (end()).
  ~GuiEngineSplash();
  GuiEngineSplash(const GuiEngineSplash&) = delete;
  GuiEngineSplash& operator=(const GuiEngineSplash&) = delete;
  GuiEngineSplash(GuiEngineSplash&&) = delete;
  GuiEngineSplash& operator=(GuiEngineSplash&&) = delete;

  // Acquires the logo from `imagePath` and starts the clock. False, and
  // finished(), when a service is missing or the logo is unavailable.
  bool begin(IRenderWindow* window,
             Renderer* renderer,
             AssetManager* assets,
             const std::string& imagePath,
             InputManager* input,
             bool reducedMotion);
  // Advances the clock, takes skip input unless `inputBlocked` (an open
  // console), and redraws. `dt` is clamped, so one long frame cannot skip
  // the splash on its own.
  void update(double dt, bool inputBlocked);
  bool finished() const { return m_finished; }
  // Releases the logo and clears the layers. Idempotent.
  void end();
  // The splash's layers, back to front, while it is showing.
  void addDrawables(DrawList& frame);

  float elapsedForTesting() const { return m_elapsed; }
  // The logo's current opacity, 0..1.
  float logoOpacityForTesting() const { return logoOpacity(); }

private:
  enum Layer : std::size_t
  {
    kGroundLayer,
    kLogoLayer,
    kLayerCount
  };

  float logoOpacity() const;
  bool takeSkip();
  void redraw();

  IRenderWindow* m_window = nullptr;
  Renderer* m_renderer = nullptr;
  AssetManager* m_assets = nullptr;
  InputManager* m_input = nullptr;
  TextureHandle m_logo{};
  float m_aspect = 0.0f;
  bool m_reducedMotion = false;
  bool m_finished = true;
  // A click skips only once the button was seen released.
  bool m_pointerWasPressed = true;
  float m_elapsed = 0.0f;
  std::array<GameVisual, kLayerCount> m_layers;
  // The ground's size when last drawn; it redraws only on resize.
  float m_groundWidth = -1.0f;
  float m_groundHeight = -1.0f;
};
