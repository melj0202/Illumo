#include <Illumo/Gui/GuiEngineBrand.h>
#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/DrawList.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestAccess.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <string>

// A headless renderer and asset manager reading the engine's own assets.
struct BrandFixture
{
  NullRenderWindow window;
  EnvVars env;
  Camera camera;
  MockBackend mock;
  Renderer renderer;
  AssetManager assets;
  InputManager input;
  DrawList frame;

  BrandFixture()
    : window(640, 480)
    , camera(glm::vec2(0.0f, 0.0f), 1.0f, &env)
    , renderer(&window, &env, &camera, &mock, false)
    , assets(&renderer, false)
    , input(nullptr)
    , frame(&window, &camera)
  {
    mock.Initialize();
  }

  static std::string engineAsset(const char* name)
  {
    return std::string(ILLUMO_ENGINE_ASSETS) + "/" + name;
  }
};

static int
testEngineBadge()
{
  TestCounters counters;
  BrandFixture fixture;
  GameVisual visual;
  GuiEngineBadge badge;
  testTrue(counters,
           !badge.acquire(nullptr) && !badge.ready() && badge.width(24.0f) == 0,
           "without an asset manager the badge is not ready");
  badge.draw(visual, 10.0f, 100.0f, 24.0f, 255);
  testEqSize(counters, visual.itemCount(), 0, "an unready badge draws nothing");

  testTrue(counters,
           badge.acquire(&fixture.assets,
                         BrandFixture::engineAsset(GuiEngineBadge::kAssetName)),
           "the engine's badge image loads");
  testTrue(counters,
           badge.width(24.0f) > 24.0f * 2.5f,
           "the wordmark is about three times as wide as it is tall");
  badge.draw(visual, 10.0f, 100.0f, 24.0f, 200);
  testEqSize(counters, visual.spriteCount(), 1, "the wordmark is one sprite");
  testEqSize(counters, visual.textCount(), 1, "the label is one text");
  const SpritePrimitive* sprite = visual.getSprite(0);
  const TextPrimitive* label = visual.getText(0);
  if (sprite != nullptr && label != nullptr) {
    testTrue(counters,
             sprite->rect.x == 10.0f && sprite->rect.y == 76.0f &&
               sprite->rect.h == 24.0f && sprite->tint.a == 200,
             "the wordmark sits on the baseline at the given opacity");
    testTrue(counters,
             label->content == "POWERED BY" && label->y < sprite->rect.y,
             "the label sits above the wordmark");
  }
  badge.release();
  testTrue(counters, !badge.ready(), "release forgets the image");
  testTrue(counters,
           !badge.acquire(&fixture.assets, "missing/badge.png"),
           "a missing image leaves the badge unready");
  return counters.failures;
}

static int
testEngineSplashTimeline()
{
  TestCounters counters;
  BrandFixture fixture;
  GuiEngineSplash splash;
  testTrue(counters, splash.finished(), "an idle splash is finished");
  testTrue(counters,
           !splash.begin(&fixture.window,
                         &fixture.renderer,
                         &fixture.assets,
                         "missing/splash.png",
                         &fixture.input,
                         false) &&
             splash.finished(),
           "a missing logo finishes the splash at once");
  splash.addDrawables(fixture.frame);
  testEqSize(counters,
             fixture.frame.drawableCount(),
             0,
             "a finished splash without a logo draws nothing");

  testTrue(counters,
           splash.begin(&fixture.window,
                        &fixture.renderer,
                        &fixture.assets,
                        BrandFixture::engineAsset(GuiEngineSplash::kAssetName),
                        &fixture.input,
                        false) &&
             !splash.finished(),
           "the splash begins with the engine's logo");
  splash.addDrawables(fixture.frame);
  testEqSize(
    counters, fixture.frame.drawableCount(), 2, "ground and logo layers");

  // One long frame advances at most a tenth of a second.
  splash.update(5.0, false);
  testTrue(counters,
           !splash.finished() && splash.elapsedForTesting() <= 0.1f + 1e-4f,
           "a long frame cannot skip the splash");
  bool stuttered = false;
  float previous = splash.logoOpacityForTesting();
  while (splash.elapsedForTesting() < GuiEngineSplash::kLightSeconds) {
    splash.update(1.0 / 120.0, false);
    stuttered = stuttered || splash.logoOpacityForTesting() < previous;
    previous = splash.logoOpacityForTesting();
  }
  testTrue(counters, stuttered, "the logo strikes with a stutter");
  splash.update(0.5, false);
  testTrue(counters,
           splash.logoOpacityForTesting() == 1.0f,
           "the logo holds at full brightness");
  int frames = 0;
  while (!splash.finished() && frames < 1000) {
    splash.update(1.0 / 60.0, false);
    ++frames;
  }
  testTrue(counters,
           splash.finished() && splash.logoOpacityForTesting() == 0.0f,
           "the logo fades out and the splash finishes");
  testTrue(counters,
           splash.elapsedForTesting() == GuiEngineSplash::kTotalSeconds,
           "the splash runs its full length");

  // Reduced motion lights the logo with a plain fade.
  splash.begin(&fixture.window,
               &fixture.renderer,
               &fixture.assets,
               BrandFixture::engineAsset(GuiEngineSplash::kAssetName),
               &fixture.input,
               true);
  previous = splash.logoOpacityForTesting();
  bool monotonic = true;
  while (splash.elapsedForTesting() < GuiEngineSplash::kLightSeconds) {
    splash.update(1.0 / 120.0, false);
    monotonic = monotonic && splash.logoOpacityForTesting() >= previous;
    previous = splash.logoOpacityForTesting();
  }
  testTrue(counters, monotonic, "reduced motion fades in without stutters");
  splash.end();
  testTrue(counters, splash.finished(), "end finishes the splash");
  return counters.failures;
}

static int
testEngineSplashSkip()
{
  TestCounters counters;
  BrandFixture fixture;
  GuiEngineSplash splash;
  const std::string logo =
    BrandFixture::engineAsset(GuiEngineSplash::kAssetName);
  InputManager& input = fixture.input;

  splash.begin(
    &fixture.window, &fixture.renderer, &fixture.assets, logo, &input, false);
  input.getKeyQueue().push({ KeyCode::Grave, InputAction::Press, 0 });
  splash.update(1.0 / 60.0, false);
  testTrue(counters,
           !splash.finished() && input.getKeyQueue().size() == 1,
           "the console key neither skips nor is taken");
  input.getKeyQueue().pop();
  input.getKeyQueue().push({ KeyCode::Space, InputAction::Press, 0 });
  splash.update(1.0 / 60.0, true);
  testTrue(
    counters, !splash.finished(), "keys do not skip while the console is open");
  splash.update(1.0 / 60.0, false);
  testTrue(counters,
           splash.finished() && input.getKeyQueue().empty(),
           "a key press skips the splash and is taken");

  // A button held from before the splash must be released first.
  InputManagerTestAccess::setAction(
    input, KeyCode::MouseLeft, InputAction::Press);
  splash.begin(
    &fixture.window, &fixture.renderer, &fixture.assets, logo, &input, false);
  splash.update(1.0 / 60.0, false);
  testTrue(counters, !splash.finished(), "a held button does not skip");
  InputManagerTestAccess::setAction(
    input, KeyCode::MouseLeft, InputAction::Release);
  splash.update(1.0 / 60.0, false);
  InputManagerTestAccess::setAction(
    input, KeyCode::MouseLeft, InputAction::Press);
  splash.update(1.0 / 60.0, false);
  testTrue(counters, splash.finished(), "a fresh click skips the splash");
  return counters.failures;
}

void
registerGuiEngineBrandTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Gui.EngineBadge", []() { return testEngineBadge(); });
  registry.add("Illumo.Gui.EngineSplashTimeline",
               []() { return testEngineSplashTimeline(); });
  registry.add("Illumo.Gui.EngineSplashSkip",
               []() { return testEngineSplashSkip(); });
}
