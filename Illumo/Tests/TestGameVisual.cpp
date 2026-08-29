// GameVisual primitive host: shapes + sprites via MockBackend (no OpenGL).

#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/Primitives/SpriteAnimation.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Rendering/WorldLook.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cmath>
#include <cstring>
#include <glm/gtc/type_ptr.hpp>

static TestCounters g;

struct CapturedSpriteVertex
{
  float x;
  float y;
  float z;
  unsigned char r;
  unsigned char green;
  unsigned char b;
  unsigned char a;
  float u;
  float v;
};

static bool
nearFloat(float left, float right)
{
  return std::abs(left - right) < 0.0001f;
}

static const RenderCommand*
findSubmittedCommand(const MockBackend& mock, CommandType type, size_t ordinal)
{
  size_t found = 0;
  for (size_t i = 0; i < mock.getLastNonEmptySubmittedCount(); ++i) {
    const RenderCommand& command = mock.getLastNonEmptySubmitted(i);
    if (command.commandType == type) {
      if (found == ordinal) {
        return &command;
      }
      found += 1;
    }
  }
  return nullptr;
}

static size_t
submittedCommandPosition(const MockBackend& mock,
                         CommandType type,
                         size_t ordinal)
{
  size_t found = 0;
  for (size_t i = 0; i < mock.getLastNonEmptySubmittedCount(); ++i) {
    if (mock.getLastNonEmptySubmittedType(i) == type) {
      if (found == ordinal) {
        return i;
      }
      found += 1;
    }
  }
  return mock.getLastNonEmptySubmittedCount();
}

class FrameContextProbe : public DrawableBase
{
public:
  void Draw() override {}

  bool AppendCommands(Renderer* renderer) override
  {
    const Renderer::FrameContext& context = renderer->getFrameContext();
    observedActive = context.active;
    observedDimensions = context.windowDimensions;
    observedCamera = context.worldCamera;
    observedHasWorldMvp = context.hasWorldMvp;
    observedMvpFirstValue = context.worldMvp[0];
    return true;
  }

  bool observedActive = false;
  bool observedHasWorldMvp = false;
  std::array<int, 2> observedDimensions{ 0, 0 };
  Camera* observedCamera = nullptr;
  float observedMvpFirstValue = 0.0f;
};

static void
testRendererFrameContext()
{
  testSection("Renderer: frame context is captured once for scene extraction");
  NullRenderWindow window(640, 480);
  EnvVars env;
  env.setVar("WinX", 640);
  env.setVar("WinY", 480);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  FrameContextProbe probe;
  Scene scene(&window, &camera);
  scene.AddDrawable(&probe, RenderLayerId::World);

  renderer.BeginFrame();
  renderer.RenderScene(&scene, &camera);
  renderer.EndFrame();

  const glm::mat4 expected = camera.GetMVPMatrix(640.0f / 480.0f);
  testTrue(g, probe.observedActive, "context is active during extraction");
  testTrue(
    g, probe.observedHasWorldMvp, "context contains the primary camera MVP");
  testEqInt(
    g, probe.observedDimensions[0], 640, "context keeps one frame width");
  testEqInt(
    g, probe.observedDimensions[1], 480, "context keeps one frame height");
  testTrue(
    g, probe.observedCamera == &camera, "context identifies the cached camera");
  testTrue(g,
           nearFloat(probe.observedMvpFirstValue, expected[0][0]),
           "context MVP matches the camera result");
  testTrue(g,
           !renderer.getFrameContext().active,
           "context expires after scene extraction");
}

static void
testGameVisualShapesEmitTokens()
{
  testSection("GameVisual: filled/outline/line shapes emit tokens");
  NullRenderWindow window(640, 480);
  EnvVars env;
  env.setVar("WinX", 640);
  env.setVar("WinY", 480);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);

  GameVisual visual;
  visual.setWindow(&window);
  visual.setCamera(&camera);
  visual.setSpace(PrimitiveSpace::Pixels);
  visual.prepare(&renderer);

  ColorRgba red{ 255, 0, 0, 255 };
  ColorRgba green{ 0, 255, 0, 255 };
  ColorRgba blue{ 0, 0, 255, 255 };
  visual.addFilledRect(10.0f, 20.0f, 40.0f, 30.0f, red);
  visual.addOutlineRect(100.0f, 100.0f, 50.0f, 50.0f, green, 2.0f);
  visual.addLine(0.0f, 0.0f, 100.0f, 0.0f, blue, 2.0f);

  testEqSize(g, visual.shapeCount(), 3u, "three shapes stored");
  testTrue(
    g, renderer.getStyle(RenderStyleId::Shape) != nullptr, "Shape style");

  mock.resetCounters();
  Scene scene(&window, &camera);
  scene.AddDrawable(&visual, RenderLayerId::UI);

  renderer.BeginFrame();
  renderer.RenderScene(&scene, &camera);
  renderer.EndFrame();

  testTrue(
    g, mock.getLastNonEmptySubmittedCount() > 0, "shape frame non-empty");
  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::UpdateBuffer),
             1u,
             "one shape buffer upload");
  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::DrawIndexed),
             1u,
             "one shape draw batch");
  testTrue(g,
           mock.countNonEmptyOfType(CommandType::SetShader) >= 1u,
           "shape bindStyle sets shader");
  testTrue(g,
           mock.countNonEmptyOfType(CommandType::SetPipelineState) >= 1u,
           "pipeline state set");
  bool foundOverlayMvp = false;
  bool overlayMatchesScreen = false;
  bool foundPixelMode = false;
  const Matrix4 expectedOverlay = WorldLook::overlayProjection(640.0f, 480.0f);
  const float* expectedPtr = glm::value_ptr(expectedOverlay);
  for (size_t i = 0; i < mock.getLastNonEmptySubmittedCount(); ++i) {
    const RenderCommand& command = mock.getLastNonEmptySubmitted(i);
    if (command.commandType == CommandType::SetUniformMat4 &&
        std::strcmp(command.uniformMat4.name, WorldLook::kMvpUniform) == 0) {
      foundOverlayMvp = true;
      overlayMatchesScreen = true;
      for (int e = 0; e < 16; ++e) {
        if (std::abs(command.uniformMat4.m[e] - expectedPtr[e]) > 0.0001f) {
          overlayMatchesScreen = false;
        }
      }
    }
    if (command.commandType == CommandType::SetUniformInt &&
        std::strcmp(command.uniformInt.name, "uUsePixels") == 0 &&
        command.uniformInt.value == 1) {
      foundPixelMode = true;
    }
  }
  testTrue(g, foundOverlayMvp, "overlay shapes push uMVP");
  testTrue(g,
           overlayMatchesScreen,
           "overlay uMVP is the Y-down screen ortho, not the world camera");
  testTrue(g, !foundPixelMode, "overlay shapes do not set uUsePixels=1");
}

static void
testGameVisualSpritesBatchByTexture()
{
  testSection("GameVisual: sprites batch by texture handle");
  NullRenderWindow window(800, 600);
  EnvVars env;
  env.setVar("WinX", 800);
  env.setVar("WinY", 600);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);

  // Enroll two textures as content handles (owner side).
  unsigned char px[4] = { 255, 255, 255, 255 };
  TextureHandle textureA = renderer.enrollTexture(px, 1, 1, 4);
  TextureHandle textureB = renderer.enrollTexture(px, 1, 1, 4);

  GameVisual visual;
  visual.setWindow(&window);
  visual.prepare(&renderer);

  ColorRgba white{ 255, 255, 255, 255 };
  // Interleave textures. Painter order is preserved, so no global texture
  // sort may combine these non-adjacent sprites.
  visual.addSprite(textureB, 0.0f, 0.0f, 16.0f, 16.0f, white);
  visual.addSprite(textureA, 20.0f, 0.0f, 16.0f, 16.0f, white);
  visual.addSprite(textureA, 40.0f, 0.0f, 16.0f, 16.0f, white);
  visual.addSprite(textureB, 60.0f, 0.0f, 16.0f, 16.0f, white);
  visual.addSprite(textureA, 80.0f, 0.0f, 16.0f, 16.0f, white);

  testEqSize(g, visual.spriteCount(), 5u, "five sprites stored");
  testTrue(
    g, renderer.getStyle(RenderStyleId::Sprite) != nullptr, "Sprite style");

  mock.resetCounters();
  Scene scene(&window, &camera);
  scene.AddDrawable(&visual, RenderLayerId::World);

  renderer.BeginFrame();
  renderer.RenderScene(&scene, &camera);
  renderer.EndFrame();

  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::UpdateBuffer),
             1u,
             "one sprite buffer upload");
  // B, A+A, B, A remains four batches: adjacent A sprites combine, while a
  // global texture sort would incorrectly combine non-adjacent runs.
  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::SetTexture),
             4u,
             "four adjacent texture runs bind independently");
  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::DrawIndexed),
             4u,
             "four painter-correct sprite draw batches");
}

static void
testGameVisualCompositionHost()
{
  testSection("GameVisual: complex object composes host + primitives");
  // Stand-in for a game object that embeds GameVisual rather than owning
  // draw tokens itself.
  struct CrossMarker
  {
    GameVisual visual;
    void build(float cx, float cy, float arm)
    {
      visual.clearPrimitives();
      ColorRgba c{ 255, 200, 50, 255 };
      visual.addLine(cx - arm, cy, cx + arm, cy, c, 2.0f);
      visual.addLine(cx, cy - arm, cx, cy + arm, c, 2.0f);
      visual.addOutlineRect(
        cx - arm, cy - arm, arm * 2.0f, arm * 2.0f, c, 1.0f);
    }
  };

  NullRenderWindow window(320, 240);
  EnvVars env;
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);

  CrossMarker marker;
  marker.visual.setWindow(&window);
  marker.visual.prepare(&renderer);
  marker.build(160.0f, 120.0f, 20.0f);

  testEqSize(g, marker.visual.shapeCount(), 3u, "composed of three shapes");

  mock.resetCounters();
  testTrue(
    g, marker.visual.AppendCommands(&renderer), "composed visual emits tokens");
  // Direct AppendCommands leaves tokens in queue; EndFrame submits.
  renderer.EndFrame();
  testTrue(g,
           mock.countNonEmptyOfType(CommandType::DrawIndexed) >= 1u,
           "composed marker draws");
}

static void
testGameVisualTextPrimitive()
{
  testSection("GameVisual: text primitive emits shape batch");
  NullRenderWindow window(640, 480);
  EnvVars env;
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);

  GameVisual visual;
  visual.setWindow(&window);
  visual.prepare(&renderer);
  ColorRgba white{ 255, 255, 255, 255 };
  visual.addText("Hi", 10.0f, 20.0f, 12.0f, white);

  mock.resetCounters();
  testTrue(g, visual.AppendCommands(&renderer), "text AppendCommands");
  renderer.EndFrame();
  testTrue(g,
           mock.countNonEmptyOfType(CommandType::DrawIndexed) >= 1u,
           "text draws via shape mesh");
  testTrue(g,
           mock.countNonEmptyOfType(CommandType::UpdateBuffer) >= 1u,
           "text uploads shape buffer");
}

static void
testGameVisualMultipleTextRuns()
{
  testSection("GameVisual: several text runs tessellate into one shape batch");
  NullRenderWindow window(640, 480);
  EnvVars env;
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);

  GameVisual visual;
  visual.setWindow(&window);
  visual.prepare(&renderer);
  ColorRgba white{ 255, 255, 255, 255 };
  visual.addText("Line one of cached tessellation", 10.0f, 20.0f, 12.0f, white);
  visual.addText("Line two of cached tessellation", 10.0f, 40.0f, 12.0f, white);
  visual.addText(
    "Line three of cached tessellation", 10.0f, 60.0f, 12.0f, white);

  mock.resetCounters();
  testTrue(g, visual.AppendCommands(&renderer), "multi-text AppendCommands");
  renderer.EndFrame();
  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::DrawIndexed),
             1u,
             "multiple text runs stay one shape draw");
  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::UpdateBuffer),
             1u,
             "multiple text runs stay one shape upload");
}

static void
testGameVisualEmptyAndInvisible()
{
  testSection("GameVisual: empty/invisible skip draws");
  NullRenderWindow window(320, 240);
  EnvVars env;
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);

  GameVisual visual;
  visual.prepare(&renderer);
  testTrue(g, visual.AppendCommands(&renderer), "empty succeeds");

  ColorRgba c{ 1, 2, 3, 255 };
  visual.addFilledRect(0, 0, 10, 10, c);
  visual.setVisible(false);
  mock.resetCounters();
  testTrue(g, visual.AppendCommands(&renderer), "invisible succeeds");
  renderer.EndFrame();
  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::DrawIndexed),
             0u,
             "invisible emits no draw");
}

static void
testGameVisualTransformAndAtlas()
{
  testSection("GameVisual: pivot rotation, atlas UVs, and flips");
  NullRenderWindow window(320, 240);
  EnvVars env;
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  unsigned char pixels[4] = { 255, 255, 255, 255 };
  TextureHandle texture = renderer.enrollTexture(pixels, 1, 1, 4);

  GameVisual visual;
  visual.setWindow(&window);
  visual.prepare(&renderer);
  TextureRegion region = TextureRegion::gridCell(4, 2, 2, 1);
  size_t index =
    visual.addSprite(texture, Rect2{ 10.0f, 20.0f, 20.0f, 10.0f }, region);
  SpritePrimitive* sprite = visual.getSprite(index);
  sprite->transform.x = 3.0f;
  sprite->transform.y = 4.0f;
  sprite->transform.scaleX = 2.0f;
  sprite->transform.pivotX = 0.5f;
  sprite->transform.pivotY = 0.5f;
  sprite->transform.rotationRadians = 1.57079632679f;
  sprite->flipX = true;
  sprite->flipY = true;
  Transform2D hostTransform;
  hostTransform.x = 5.0f;
  hostTransform.y = 7.0f;
  visual.setTransform(hostTransform);

  mock.resetCounters();
  visual.AppendCommands(&renderer);
  renderer.EndFrame();
  const RenderCommand* update =
    findSubmittedCommand(mock, CommandType::UpdateBuffer, 0);
  testTrue(g, update != nullptr, "sprite vertices are uploaded");
  if (update != nullptr) {
    const CapturedSpriteVertex* vertices =
      static_cast<const CapturedSpriteVertex*>(update->updateBuffer.data);
    testTrue(g,
             nearFloat(vertices[0].x, 33.0f) && nearFloat(vertices[0].y, 16.0f),
             "local scale/position and parent translation transform corner");
    testTrue(g,
             nearFloat(vertices[2].x, 23.0f) && nearFloat(vertices[2].y, 56.0f),
             "center pivot rotation transforms the opposite corner");
    testTrue(g,
             nearFloat(vertices[0].u, 0.75f) && nearFloat(vertices[0].v, 1.0f),
             "grid region and both flips map the first UV");
    testTrue(g,
             nearFloat(vertices[2].u, 0.5f) && nearFloat(vertices[2].v, 0.5f),
             "grid region and both flips map the opposite UV");
  }
}

static void
testGameVisualDynamicCapacity()
{
  testSection("GameVisual: dynamic quad growth and safety limit");
  NullRenderWindow window(320, 240);
  EnvVars env;
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  ColorRgba color{ 255, 255, 255, 255 };

  GameVisual growing;
  growing.prepare(&renderer);
  for (unsigned int i = 0; i < 1025; ++i) {
    growing.addFilledRect(static_cast<float>(i), 0.0f, 1.0f, 1.0f, color);
  }
  growing.AppendCommands(&renderer);
  renderer.EndFrame();
  testEqInt(g,
            static_cast<int>(growing.getQuadCapacity()),
            2048,
            "capacity doubles from 1024 to 2048");

  GameVisual capped(1024);
  capped.prepare(&renderer);
  for (unsigned int i = 0; i < 1025; ++i) {
    capped.addFilledRect(static_cast<float>(i), 2.0f, 1.0f, 1.0f, color);
  }
  mock.resetCounters();
  capped.AppendCommands(&renderer);
  renderer.EndFrame();
  const RenderCommand* draw =
    findSubmittedCommand(mock, CommandType::DrawIndexed, 0);
  testTrue(g, draw != nullptr, "capped visual still draws accepted quads");
  if (draw != nullptr) {
    testEqInt(g,
              static_cast<int>(draw->drawIndexed.elementCount),
              1024 * 6,
              "safety limit rejects geometry beyond configured maximum");
  }
}

static void
testGameVisualCrossTypeOrder()
{
  testSection("GameVisual: stable cross-type draw order");
  NullRenderWindow window(320, 240);
  EnvVars env;
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  unsigned char pixels[4] = { 255, 255, 255, 255 };
  TextureHandle texture = renderer.enrollTexture(pixels, 1, 1, 4);
  GameVisual visual;
  visual.prepare(&renderer);
  size_t shapeIndex =
    visual.addFilledRect(0.0f, 0.0f, 32.0f, 32.0f, ColorRgba{ 255, 0, 0, 180 });
  size_t spriteIndex = visual.addSprite(
    texture, 8.0f, 8.0f, 32.0f, 32.0f, ColorRgba{ 255, 255, 255, 180 });
  visual.getShape(shapeIndex)->drawOrder = 0;
  visual.getSprite(spriteIndex)->drawOrder = 0;

  mock.resetCounters();
  visual.AppendCommands(&renderer);
  renderer.EndFrame();
  size_t firstDraw =
    submittedCommandPosition(mock, CommandType::DrawIndexed, 0);
  size_t firstTexture =
    submittedCommandPosition(mock, CommandType::SetTexture, 0);
  testTrue(g,
           firstDraw < firstTexture,
           "equal order preserves shape-before-sprite insertion sequence");

  visual.getSprite(spriteIndex)->drawOrder = -1;
  mock.resetCounters();
  visual.AppendCommands(&renderer);
  renderer.EndFrame();
  firstDraw = submittedCommandPosition(mock, CommandType::DrawIndexed, 0);
  firstTexture = submittedCommandPosition(mock, CommandType::SetTexture, 0);
  testTrue(g,
           firstTexture < firstDraw,
           "explicit sprite order moves it before the shape");
}

static SpriteAnimationClip
makeAnimationClip(SpriteLoopMode mode)
{
  SpriteAnimationClip clip;
  clip.loopMode = mode;
  for (unsigned int i = 0; i < 3; ++i) {
    SpriteAnimationFrame frame;
    frame.region = TextureRegion::gridCell(4, 1, i, 0);
    frame.durationSeconds = 0.1;
    clip.frames.push_back(frame);
  }
  return clip;
}

static void
testSpriteAnimationModes()
{
  testSection("SpriteAnimator: once, loop, ping-pong, pause, and reset");
  SpriteAnimationClip onceClip = makeAnimationClip(SpriteLoopMode::Once);
  SpriteAnimator once;
  once.setClip(&onceClip);
  once.update(0.31);
  testEqSize(g, once.getFrameIndex(), 2u, "once stops on final frame");
  testTrue(g, !once.isPlaying(), "once mode pauses at completion");

  SpriteAnimationClip loopClip = makeAnimationClip(SpriteLoopMode::Loop);
  SpriteAnimator loop;
  loop.setClip(&loopClip);
  loop.update(0.31);
  testEqSize(g, loop.getFrameIndex(), 0u, "loop wraps to first frame");
  loop.pause();
  loop.update(1.0);
  testEqSize(g, loop.getFrameIndex(), 0u, "paused animator does not advance");
  loop.play();
  loop.update(0.11);
  testEqSize(g, loop.getFrameIndex(), 1u, "play resumes advancement");
  loop.reset();
  testEqSize(g, loop.getFrameIndex(), 0u, "reset restores first frame");

  SpriteAnimationClip pingPongClip =
    makeAnimationClip(SpriteLoopMode::PingPong);
  SpriteAnimator pingPong;
  pingPong.setClip(&pingPongClip);
  pingPong.update(0.11);
  testEqSize(g, pingPong.getFrameIndex(), 1u, "ping-pong advances");
  pingPong.update(0.11);
  testEqSize(g, pingPong.getFrameIndex(), 2u, "ping-pong reaches end");
  pingPong.update(0.11);
  testEqSize(g, pingPong.getFrameIndex(), 1u, "ping-pong reverses");
  pingPong.update(0.11);
  testEqSize(g, pingPong.getFrameIndex(), 0u, "ping-pong reaches start");
  testTrue(g,
           nearFloat(pingPong.currentRegion().u0, 0.0f),
           "current region follows frame index");
}

static void
testCustomStyleRegistry()
{
  testSection("Renderer: custom 2D style registry lifecycle");
  NullRenderWindow window(320, 240);
  EnvVars env;
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  GameVisual visual;
  visual.prepare(&renderer);

  ShaderSources sources;
  sources.vertexSource = "custom vertex";
  sources.fragmentSource = "custom fragment";
  ShaderHandle shader = renderer.enrollShader(sources);
  RenderStyle style = *renderer.getStyle(RenderStyleId::Sprite);
  style.shaderHandle = shader;
  RenderStyleHandle styleHandle = renderer.createStyle(style);
  testTrue(g, styleHandle.isValid(), "custom style gets typed handle");

  unsigned char pixels[4] = { 255, 255, 255, 255 };
  TextureHandle texture = renderer.enrollTexture(pixels, 1, 1, 4);
  size_t spriteIndex =
    visual.addSprite(texture, 0.0f, 0.0f, 16.0f, 16.0f, ColorRgba{});
  visual.getSprite(spriteIndex)->styleHandle = styleHandle;
  mock.resetCounters();
  visual.AppendCommands(&renderer);
  renderer.EndFrame();

  const RenderCommand* shaderBind =
    findSubmittedCommand(mock, CommandType::SetShader, 0);
  testTrue(g,
           shaderBind != nullptr && shaderBind->bindShader.handle == shader,
           "primitive binds custom style shader through normal contract");
  testTrue(g, renderer.destroyStyle(styleHandle), "custom style destroys");
  testTrue(g,
           renderer.getStyle(styleHandle) == nullptr,
           "destroyed style handle is stale");
  testTrue(g,
           !renderer.destroyStyle(styleHandle),
           "stale style destroy safely no-ops");
}

static int
runGameVisualCase(void (*testFunction)())
{
  g.failures = 0;
  testFunction();
  return g.failures;
}

void
registerGameVisualTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Renderer.FrameContext",
               []() { return runGameVisualCase(testRendererFrameContext); });
  registry.add("Illumo.GameVisual.Shapes", []() {
    return runGameVisualCase(testGameVisualShapesEmitTokens);
  });
  registry.add("Illumo.GameVisual.SpriteBatches", []() {
    return runGameVisualCase(testGameVisualSpritesBatchByTexture);
  });
  registry.add("Illumo.GameVisual.Composition", []() {
    return runGameVisualCase(testGameVisualCompositionHost);
  });
  registry.add("Illumo.GameVisual.Text",
               []() { return runGameVisualCase(testGameVisualTextPrimitive); });
  registry.add("Illumo.GameVisual.MultipleTextRuns", []() {
    return runGameVisualCase(testGameVisualMultipleTextRuns);
  });
  registry.add("Illumo.GameVisual.EmptyAndInvisible", []() {
    return runGameVisualCase(testGameVisualEmptyAndInvisible);
  });
  registry.add("Illumo.GameVisual.TransformAndAtlas", []() {
    return runGameVisualCase(testGameVisualTransformAndAtlas);
  });
  registry.add("Illumo.GameVisual.DynamicCapacity", []() {
    return runGameVisualCase(testGameVisualDynamicCapacity);
  });
  registry.add("Illumo.GameVisual.CrossTypeOrder", []() {
    return runGameVisualCase(testGameVisualCrossTypeOrder);
  });
  registry.add("Illumo.GameVisual.AnimationModes",
               []() { return runGameVisualCase(testSpriteAnimationModes); });
  registry.add("Illumo.GameVisual.CustomStyleRegistry",
               []() { return runGameVisualCase(testCustomStyleRegistry); });
}
