#include <Illumo/Gui/GuiEngineBrand.h>

#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/DrawList.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <cmath>
#include <queue>
#include <string>

// Brand colors (docs/brand/README.md): the neon palette's background
// gradient, glow and letters.
static const ColorRgba kGroundEdge{ 7, 5, 3, 255 };
static const ColorRgba kGroundCenter{ 28, 19, 8, 255 };
static const ColorRgba kNeonGlow{ 255, 150, 40, 255 };
static const ColorRgba kLetters{ 255, 241, 214, 255 };
static const char* kBadgeLabel = "POWERED BY";
static const char* kSplashLabel = "MADE WITH";

// A brand image with linear filtering and its width over height, or an
// invalid handle (and aspect 0) when it cannot be used.
static TextureHandle
acquireBrandImage(AssetManager* assets, const std::string& path, float& aspect)
{
  aspect = 0.0f;
  if (assets == nullptr) {
    return {};
  }
  TextureOptions options;
  options.filter = TextureFilter::Linear;
  const TextureHandle image =
    assets->acquireTexture(path, options, AssetLoadMode::Synchronous);
  const TextureInfo info = assets->getTextureInfo(image);
  if (!image.isValid() || assets->getState(image).state != AssetState::Ready ||
      info.width <= 0 || info.height <= 0) {
    if (image.isValid()) {
      assets->releaseTexture(image);
    }
    Logger::LogWarning(std::string("Engine brand image ") + path +
                       " is unavailable");
    return {};
  }
  aspect = static_cast<float>(info.width) / static_cast<float>(info.height);
  return image;
}

static unsigned char
alphaOf(float opacity, unsigned char alpha)
{
  return static_cast<unsigned char>(std::clamp(opacity, 0.0f, 1.0f) *
                                    static_cast<float>(alpha));
}

GuiEngineBadge::~GuiEngineBadge()
{
  release();
}

bool
GuiEngineBadge::acquire(AssetManager* assets, const std::string& path)
{
  release();
  m_image = acquireBrandImage(assets, path, m_aspect);
  m_assets = m_image.isValid() ? assets : nullptr;
  return ready();
}

void
GuiEngineBadge::release()
{
  if (m_assets != nullptr && m_image.isValid()) {
    m_assets->releaseTexture(m_image);
  }
  m_assets = nullptr;
  m_image = TextureHandle{};
  m_aspect = 0.0f;
}

float
GuiEngineBadge::width(float height) const
{
  if (!ready()) {
    return 0.0f;
  }
  const float labelSize = height * 0.34f;
  return std::max(height * m_aspect,
                  GuiKit::measureEmphasizedText(kBadgeLabel, labelSize, 0.0f));
}

void
GuiEngineBadge::draw(GameVisual& visual,
                     float left,
                     float bottom,
                     float height,
                     unsigned char opacity) const
{
  if (!ready()) {
    return;
  }
  const float labelSize = height * 0.34f;
  const float gap = height * 0.18f;
  visual.addText(
    kBadgeLabel,
    left + height * 0.04f,
    bottom - height - gap - labelSize,
    labelSize,
    ColorRgba{ kLetters.r,
               kLetters.g,
               kLetters.b,
               alphaOf(static_cast<float>(opacity) / 255.0f, 140) });
  visual.addSprite(m_image,
                   left,
                   bottom - height,
                   height * m_aspect,
                   height,
                   ColorRgba{ 255, 255, 255, opacity });
}

GuiEngineSplash::GuiEngineSplash() = default;

GuiEngineSplash::~GuiEngineSplash()
{
  end();
}

bool
GuiEngineSplash::begin(IRenderWindow* window,
                       Renderer* renderer,
                       AssetManager* assets,
                       const std::string& imagePath,
                       InputManager* input,
                       bool reducedMotion)
{
  end();
  if (window == nullptr || renderer == nullptr) {
    return false;
  }
  m_logo = acquireBrandImage(assets, imagePath, m_aspect);
  if (!m_logo.isValid()) {
    return false;
  }
  m_window = window;
  m_renderer = renderer;
  m_assets = assets;
  m_input = input;
  m_reducedMotion = reducedMotion;
  m_finished = false;
  m_elapsed = 0.0f;
  // A button already down when the splash appears must be let go first.
  m_pointerWasPressed =
    input == nullptr || input->isMouseButtonPressed(KeyCode::MouseLeft);
  for (GameVisual& layer : m_layers) {
    layer.setSpace(PrimitiveSpace::Pixels);
    layer.setLayerHint(RenderLayerId::UI);
    layer.setWindow(window);
    layer.setRenderer(renderer);
    layer.prepare(renderer);
  }
  m_groundWidth = -1.0f;
  m_groundHeight = -1.0f;
  redraw();
  return true;
}

void
GuiEngineSplash::update(double dt, bool inputBlocked)
{
  if (m_finished) {
    return;
  }
  const float step =
    std::isfinite(dt) ? std::clamp(static_cast<float>(dt), 0.0f, 0.1f) : 0.0f;
  m_elapsed = std::min(kTotalSeconds, m_elapsed + step);
  if ((!inputBlocked && takeSkip()) || m_elapsed >= kTotalSeconds) {
    m_elapsed = kTotalSeconds;
    m_finished = true;
  }
  redraw();
}

bool
GuiEngineSplash::takeSkip()
{
  if (m_input == nullptr) {
    return false;
  }
  bool skip = false;
  std::queue<InputManager::KeyPressEvent>& keys = m_input->getKeyQueue();
  std::queue<InputManager::KeyPressEvent> remaining;
  while (!keys.empty()) {
    const InputManager::KeyPressEvent event = keys.front();
    keys.pop();
    // The debug overlay owns the console toggle.
    if (event.key == KeyCode::Grave) {
      remaining.push(event);
    } else if (event.action == InputAction::Press) {
      skip = true;
    }
  }
  std::swap(keys, remaining);
  const bool pressed = m_input->isMouseButtonPressed(KeyCode::MouseLeft);
  skip = skip || (pressed && !m_pointerWasPressed);
  m_pointerWasPressed = pressed;
  return skip;
}

float
GuiEngineSplash::logoOpacity() const
{
  const float t = m_elapsed;
  if (t >= kLightSeconds + kHoldSeconds) {
    const float fade =
      std::clamp((t - kLightSeconds - kHoldSeconds) / kFadeSeconds, 0.0f, 1.0f);
    return 1.0f - GuiEasing::inOutCubic(fade);
  }
  if (t >= kLightSeconds) {
    return 1.0f;
  }
  if (m_reducedMotion) {
    return GuiEasing::outCubic(t / kLightSeconds);
  }
  // A neon tube striking: two stutters, then it warms to full brightness.
  if (t < 0.06f) {
    return 0.85f;
  }
  if (t < 0.13f) {
    return 0.08f;
  }
  if (t < 0.18f) {
    return 0.6f;
  }
  if (t < 0.26f) {
    return 0.12f;
  }
  const float warm = (t - 0.26f) / (kLightSeconds - 0.26f);
  return 0.3f + 0.7f * GuiEasing::outCubic(warm);
}

void
GuiEngineSplash::redraw()
{
  const GuiPanelFit viewport = GuiPanelLayout::viewport(m_window, m_renderer);
  const float width = viewport.virtualWidth;
  const float height = viewport.virtualHeight;

  GameVisual& ground = m_layers[kGroundLayer];
  if (width != m_groundWidth || height != m_groundHeight) {
    m_groundWidth = width;
    m_groundHeight = height;
    ground.clearPrimitives();
    ground.addFilledRect(0.0f, 0.0f, width, height, kGroundEdge);
    GuiKit::drawSoftGlow(ground,
                         width * 0.5f,
                         height * 0.5f,
                         width * 0.75f,
                         height * 0.75f,
                         kGroundCenter,
                         32);
    ground.setVisible(true);
  }

  GameVisual& logo = m_layers[kLogoLayer];
  logo.clearPrimitives();
  float logoWidth = std::min(width * 0.46f, 560.0f);
  float logoHeight = logoWidth / m_aspect;
  if (logoHeight > height * 0.5f) {
    logoHeight = height * 0.5f;
    logoWidth = logoHeight * m_aspect;
  }
  const float centerX = width * 0.5f;
  const float centerY = height * 0.47f;
  const float bottom = centerY - logoHeight * 0.5f;
  const float opacity = logoOpacity();
  ColorRgba glow = kNeonGlow;
  glow.a = alphaOf(opacity, 36);
  GuiKit::drawSoftGlow(
    logo, centerX, centerY, logoWidth * 0.8f, logoHeight * 1.2f, glow, 32);
  logo.addSprite(m_logo,
                 centerX - logoWidth * 0.5f,
                 bottom,
                 logoWidth,
                 logoHeight,
                 ColorRgba{ 255, 255, 255, alphaOf(opacity, 255) });
  // "MADE WITH" sits above the logo (smaller y is higher in pixel space)
  // and fades with it.
  const float labelSize = logoHeight * 0.09f;
  const float labelGap = logoHeight * 0.14f;
  const float labelWidth =
    GuiKit::measureEmphasizedText(kSplashLabel, labelSize, 0.0f);
  logo.addText(
    kSplashLabel,
    centerX - labelWidth * 0.5f,
    bottom - labelGap - labelSize,
    labelSize,
    ColorRgba{ kLetters.r, kLetters.g, kLetters.b, alphaOf(opacity, 140) });
  logo.setVisible(true);
}

void
GuiEngineSplash::end()
{
  if (m_assets != nullptr && m_logo.isValid()) {
    m_assets->releaseTexture(m_logo);
  }
  m_assets = nullptr;
  m_logo = TextureHandle{};
  m_aspect = 0.0f;
  m_input = nullptr;
  m_finished = true;
  for (GameVisual& layer : m_layers) {
    layer.clearPrimitives();
  }
}

void
GuiEngineSplash::addDrawables(DrawList& frame)
{
  if (!m_logo.isValid()) {
    return;
  }
  for (GameVisual& layer : m_layers) {
    frame.AddDrawable(&layer, RenderLayerId::UI);
  }
}
