#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Font.h>
#include <Illumo/Rendering/FontWeightRamp.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cmath>
#include <memory>

static TestCounters g;

static void
testDefaultFontLoading()
{
  testSection("Font: default font loading and atlas generation");
  std::shared_ptr<Font> font = Font::getDefaultFont();
  testTrue(g, font != nullptr, "default font is not null");
  testTrue(g, font->isValid(), "default font is valid");
  testTrue(g, font->getAtlasWidth() > 0, "atlas width > 0");
  testTrue(g, font->getAtlasHeight() > 0, "atlas height > 0");
  testTrue(
    g, !font->getAtlasPixels().empty(), "atlas pixel buffer is populated");
  testTrue(g, font->getMetrics().lineHeight > 0.0f, "metrics lineHeight > 0");
  testTrue(g, font->getMetrics().ascender > 0.0f, "metrics ascender > 0");
}

static void
testGlyphLookupAndMetrics()
{
  testSection("Font: glyph lookup and metric properties");
  std::shared_ptr<Font> font = Font::getDefaultFont();
  testTrue(g, font != nullptr, "default font available");

  const GlyphInfo* gA = font->getGlyph('A');
  testTrue(g, gA != nullptr, "glyph 'A' found");
  if (gA != nullptr) {
    testTrue(g, gA->visible, "glyph 'A' is visible");
    testTrue(g, gA->advanceX > 0.0f, "glyph 'A' advanceX > 0");
    testTrue(g, gA->width > 0.0f, "glyph 'A' width > 0");
    testTrue(g, gA->height > 0.0f, "glyph 'A' height > 0");
    testTrue(g, gA->u1 > gA->u0, "glyph 'A' u1 > u0");
    testTrue(g, gA->v1 > gA->v0, "glyph 'A' v1 > v0");
  }

  const GlyphInfo* gSpace = font->getGlyph(' ');
  testTrue(g, gSpace != nullptr, "glyph ' ' found");
  if (gSpace != nullptr) {
    testTrue(g, gSpace->advanceX > 0.0f, "glyph ' ' advanceX > 0");
  }

  const GlyphInfo* gUnknown = font->getGlyph(0x1F600); // Emoji / non-ASCII
  testTrue(
    g, gUnknown != nullptr, "unknown codepoint resolves to fallback glyph");
}

static void
testTextMeasurement()
{
  testSection("Font: text measurement single and multi-line");
  std::shared_ptr<Font> font = Font::getDefaultFont();
  testTrue(g, font != nullptr, "default font available");

  TextBounds single = font->measureText("Hello World", 24.0f);
  testTrue(g, single.width > 0.0f, "single line text width > 0");
  testTrue(g, single.height > 0.0f, "single line text height > 0");

  TextBounds shortText = font->measureText("Hi", 24.0f);
  testTrue(
    g, single.width > shortText.width, "longer string has wider measurement");

  TextBounds multi = font->measureText("Line 1\nLine 2\nLine 3", 24.0f);
  testTrue(g,
           multi.height >= single.height * 2.5f,
           "multi-line height scales with line count");

  TextBounds empty = font->measureText("", 24.0f);
  testTrue(g, empty.width == 0.0f, "empty text width is 0");

  float advA = font->getAdvance('A', 24.0f);
  testTrue(g, advA > 0.0f, "getAdvance('A') > 0");
  float advSmallA = font->getAdvance('A', 12.0f);
  testTrue(g,
           std::abs(advA - advSmallA * 2.0f) < 0.1f,
           "getAdvance scales linearly with sizePt");
}

static void
testFontFallbackOnInvalidPath()
{
  testSection("Font: procedural fallback on non-existent file");
  std::shared_ptr<Font> badFont =
    Font::loadFromFile("path/does/not/exist/font.ttf", 32.0f);
  testTrue(g, badFont != nullptr, "fallback font created on invalid path");
  if (badFont != nullptr) {
    testTrue(g, badFont->isValid(), "fallback font is marked valid");
    TextBounds b = badFont->measureText("Test", 16.0f);
    testTrue(g, b.width > 0.0f, "fallback font measures text width > 0");
    testTrue(g, b.height > 0.0f, "fallback font measures text height > 0");
  }
}

static void
testFontTextureEnrollment()
{
  testSection("Font: GPU texture enrollment and caching");
  NullRenderWindow window(640, 480);
  EnvVars env;
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);

  std::shared_ptr<Font> font = Font::getDefaultFont();
  testTrue(g, font != nullptr, "default font available");
  if (font != nullptr) {
    TextureHandle handle1 = font->getTextureHandle(&renderer);
    testTrue(g, handle1.isValid(), "enrolled texture handle is valid");
    TextureHandle handle2 = font->getTextureHandle(&renderer);
    testTrue(
      g, handle1 == handle2, "consecutive calls reuse enrolled texture handle");
  }
}

static void
testFontGameVisualRendering()
{
  testSection("Font: GameVisual textured text primitive rendering");
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
  visual.addText("Sample text string", 10.0f, 20.0f, 16.0f, white);

  mock.resetCounters();
  testTrue(g, visual.AppendCommands(&renderer), "AppendCommands succeeds");
  renderer.EndFrame();

  testTrue(g,
           mock.countNonEmptyOfType(CommandType::DrawIndexed) >= 1u,
           "text draws via DrawIndexed");
  testTrue(g,
           mock.countNonEmptyOfType(CommandType::UpdateBuffer) >= 1u,
           "text updates sprite buffer");
}

static void
testRendererLifetimeEnrollment()
{
  NullRenderWindow window(128, 128);
  Camera camera;
  MockBackend firstBackend;
  MockBackend secondBackend;
  firstBackend.Initialize();
  secondBackend.Initialize();
  std::shared_ptr<Font> font = Font::getDefaultFont();
  alignas(Renderer) std::byte storage[sizeof(Renderer)];
  Renderer* first = std::construct_at(reinterpret_cast<Renderer*>(storage),
                                      &window,
                                      nullptr,
                                      &camera,
                                      &firstBackend,
                                      false);
  const TextureHandle old = font->getTextureHandle(first);
  testTrue(g, firstBackend.IsTextureValid(old), "first lifetime enrolls atlas");
  std::destroy_at(first);
  testTrue(g,
           !font->getTextureHandle(nullptr).isValid(),
           "expired lifetime does not expose a stale last handle");
  Renderer* second = std::construct_at(reinterpret_cast<Renderer*>(storage),
                                       &window,
                                       nullptr,
                                       &camera,
                                       &secondBackend,
                                       false);
  const unsigned char pixel[4] = { 1, 2, 3, 255 };
  const TextureHandle unrelated = second->enrollTexture(pixel, 1, 1);
  testTrue(g,
           unrelated == old,
           "fixture aliases old structural handle in new backend");
  const TextureHandle current = font->getTextureHandle(second);
  testTrue(g,
           current != unrelated && secondBackend.IsTextureValid(current),
           "same renderer address enrolls a new atlas instead of aliasing "
           "unrelated texture");
  Renderer alternate(&window, nullptr, &camera, &firstBackend, false);
  const TextureHandle alternateAtlas = font->getTextureHandle(&alternate);
  const size_t firstCount = firstBackend.getCreateCount();
  const size_t secondCount = secondBackend.getCreateCount();
  for (int i = 0; i < 8; ++i) {
    testTrue(g,
             font->getTextureHandle(second) == current &&
               font->getTextureHandle(&alternate) == alternateAtlas,
             "alternating live renderers reuses each atlas");
  }
  testTrue(g,
           firstBackend.getCreateCount() == firstCount &&
             secondBackend.getCreateCount() == secondCount,
           "alternating renderers performs no repeated enrollment");
  second->destroyTexture(current);
  const TextureHandle renewed = font->getTextureHandle(second);
  testTrue(g,
           renewed != current && secondBackend.IsTextureValid(renewed),
           "explicitly retired atlas is enrolled again");
  std::destroy_at(second);
}

static const char* kKikutaPath =
  ILLUMO_ENGINE_ASSETS "/Fonts/Kikuta/Kikuta-Variable.ttf";
static const char* kSpaceMonoPath =
  ILLUMO_ENGINE_ASSETS "/Fonts/Space_Mono/SpaceMono-Regular.ttf";

static double
atlasCoverage(const Font& font)
{
  double total = 0.0;
  const std::vector<unsigned char>& pixels = font.getAtlasPixels();
  for (size_t index = 3; index < pixels.size(); index += 4) {
    total += pixels[index];
  }
  return total;
}

static void
testVariableFace()
{
  testSection("Font: variable weight, fallback face and glyph subsets");
  FontFaceOptions thin;
  thin.weight = 200.0f;
  FontFaceOptions black;
  black.weight = 950.0f;
  Font light;
  Font heavy;
  testTrue(g,
           light.loadFile(kKikutaPath, 32.0f, thin) &&
             heavy.loadFile(kKikutaPath, 32.0f, black),
           "the variable face loads at two weights");
  testTrue(g,
           atlasCoverage(heavy) > atlasCoverage(light) * 1.2,
           "the heavier instance inks noticeably more of each glyph");
  testTrue(g,
           heavy.getAdvance('M', 32.0f) > light.getAdvance('M', 32.0f),
           "heavier letters advance further");

  // Kikuta has no '>'; the fallback face supplies it.
  Font mono;
  testTrue(g, mono.loadFile(kSpaceMonoPath, 32.0f), "fallback face loads");
  FontFaceOptions withFallback;
  withFallback.weight = 400.0f;
  withFallback.fallbackPath = kSpaceMonoPath;
  Font merged;
  testTrue(g,
           merged.loadFile(kKikutaPath, 32.0f, withFallback),
           "the face loads with a fallback");
  const GlyphInfo* chevron = merged.getGlyph('>');
  const GlyphInfo* monoChevron = mono.getGlyph('>');
  testTrue(g,
           chevron != nullptr && monoChevron != nullptr && chevron->visible &&
             chevron->width == monoChevron->width &&
             chevron->advanceX == monoChevron->advanceX,
           "missing glyphs are rasterized from the fallback face");
  testTrue(g,
           merged.getAdvance('W', 32.0f) != mono.getAdvance('W', 32.0f),
           "glyphs the face has stay its own");

  FontFaceOptions subset;
  subset.glyphs = "CSIM";
  Font word;
  testTrue(
    g, word.loadFile(kKikutaPath, 32.0f, subset), "a glyph subset loads");
  const GlyphInfo* c = word.getGlyph('C');
  const GlyphInfo* m = word.getGlyph('M');
  const GlyphInfo* space = word.getGlyph(' ');
  const GlyphInfo* q = word.getGlyph('Q');
  testTrue(g,
           c != nullptr && c->codepoint == U'C' && m != nullptr &&
             m->codepoint == U'M' && space != nullptr &&
             space->codepoint == U' ',
           "the subset keeps its glyphs and space");
  testTrue(g,
           q != nullptr && q->codepoint == U'?',
           "characters outside the subset resolve to '?'");

  Font fixed;
  FontFaceOptions ignored;
  ignored.weight = 900.0f;
  testTrue(g,
           fixed.loadFile(kSpaceMonoPath, 32.0f, ignored) &&
             fixed.getAdvance('A', 32.0f) == mono.getAdvance('A', 32.0f),
           "static faces ignore the weight");
}

static void
testWeightRamp()
{
  testSection("FontWeightRamp: sample choice, blending and measurement");
  FontFaceOptions regular;
  regular.weight = 400.0f;
  FontFaceOptions bold;
  bold.weight = 800.0f;
  std::shared_ptr<Font> light = std::make_shared<Font>();
  std::shared_ptr<Font> heavy = std::make_shared<Font>();
  light->loadFile(kKikutaPath, 32.0f, regular);
  heavy->loadFile(kKikutaPath, 32.0f, bold);
  const FontWeightRamp ramp(
    { { 800.0f, heavy }, { 400.0f, light } }, 400.0f, 800.0f);
  testTrue(g, ramp.ready(), "loaded samples make the ramp ready");
  testTrue(g,
           ramp.weightFor(0.0f) == 400.0f && ramp.weightFor(1.0f) == 800.0f &&
             ramp.weightFor(1.25f) == 900.0f,
           "emphasis maps rest to emphasis weight and past it");

  TextPrimitive text;
  testTrue(g, ramp.apply(text, 600.0f), "a weight between samples applies");
  testTrue(g,
           text.font == light && text.heavyFont == heavy &&
             std::abs(text.heavyBlend - 0.5f) < 1e-5f,
           "between samples, the heavier overlays at the fraction");
  ramp.apply(text, 400.0f);
  testTrue(g,
           text.font == light && text.heavyFont == nullptr,
           "a sample's own weight draws that sample alone");
  ramp.apply(text, 1000.0f);
  testTrue(g,
           text.font == heavy && text.heavyFont == nullptr,
           "weights past the ramp clamp to its heaviest sample");
  ramp.apply(text, 100.0f);
  testTrue(g,
           text.font == light && text.heavyFont == nullptr,
           "weights below the ramp clamp to its lightest sample");
  const float lightWidth = light->measureText("Settings", 20.0f).width;
  const float heavyWidth = heavy->measureText("Settings", 20.0f).width;
  testTrue(g,
           std::abs(ramp.measure("Settings", 20.0f, 700.0f) -
                    (lightWidth + (heavyWidth - lightWidth) * 0.75f)) < 1e-3f,
           "measurement interpolates like the drawn advances");

  std::shared_ptr<Font> pending = std::make_shared<Font>();
  const FontWeightRamp loading(
    { { 400.0f, pending }, { 800.0f, heavy } }, 400.0f, 800.0f);
  testTrue(g, !loading.ready(), "a pending sample keeps the ramp unready");
  testTrue(g,
           loading.apply(text, 500.0f) && text.font == heavy &&
             text.heavyFont == nullptr,
           "pending samples are skipped");
  const FontWeightRamp waiting({ { 400.0f, pending } }, 400.0f, 800.0f);
  testTrue(g,
           !waiting.apply(text, 400.0f) && text.font == pending &&
             waiting.measure("A", 12.0f, 400.0f) < 0.0f,
           "with nothing loaded the text waits on the first sample");
  text.font = light;
  testTrue(g,
           !FontWeightRamp().apply(text, 900.0f) && text.font == light,
           "an empty ramp leaves the text alone");
}

static void
testHeavyOverlayRendering()
{
  testSection("GameVisual: a heavier weight overlays the text run");
  NullRenderWindow window(640, 480);
  EnvVars env;
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  FontFaceOptions regular;
  regular.weight = 400.0f;
  FontFaceOptions bold;
  bold.weight = 800.0f;
  std::shared_ptr<Font> light = std::make_shared<Font>();
  std::shared_ptr<Font> heavy = std::make_shared<Font>();
  light->loadFile(kKikutaPath, 32.0f, regular);
  heavy->loadFile(kKikutaPath, 32.0f, bold);

  GameVisual visual;
  visual.setWindow(&window);
  visual.prepare(&renderer);
  const size_t index =
    visual.addText("MM", 10.0f, 20.0f, 16.0f, ColorRgba{ 255, 255, 255, 255 });
  visual.getText(index)->font = light;
  testTrue(g, visual.AppendCommands(&renderer), "plain run draws");
  renderer.EndFrame();
  const unsigned int plainQuads = visual.builtQuadCount();

  visual.getText(index)->heavyFont = heavy;
  visual.getText(index)->heavyBlend = 0.5f;
  mock.resetCounters();
  testTrue(g, visual.AppendCommands(&renderer), "blended run draws");
  renderer.EndFrame();
  testTrue(g,
           plainQuads == 2u && visual.builtQuadCount() == 4u,
           "the heavier weight adds one quad per glyph");
  testTrue(g,
           mock.countNonEmptyOfType(CommandType::DrawIndexed) >= 2u,
           "each weight's atlas draws in its own batch");

  visual.getText(index)->heavyFont = std::make_shared<Font>();
  testTrue(g, visual.AppendCommands(&renderer), "pending overlay draws");
  renderer.EndFrame();
  testTrue(
    g, visual.builtQuadCount() == 2u, "a pending heavier weight is skipped");
}

void
registerFontTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Font.VariableFace", []() {
    g = {};
    testVariableFace();
    return g.failures;
  });
  registry.add("Illumo.Font.WeightRamp", []() {
    g = {};
    testWeightRamp();
    return g.failures;
  });
  registry.add("Illumo.Font.HeavyOverlayRendering", []() {
    g = {};
    testHeavyOverlayRendering();
    return g.failures;
  });
  registry.add("Illumo.Font.RendererLifetimeEnrollment", []() {
    g = {};
    testRendererLifetimeEnrollment();
    return g.failures;
  });
  registry.add("Illumo.Font.DefaultFontLoading", []() {
    g = {};
    testDefaultFontLoading();
    return g.failures;
  });
  registry.add("Illumo.Font.GlyphLookupAndMetrics", []() {
    g = {};
    testGlyphLookupAndMetrics();
    return g.failures;
  });
  registry.add("Illumo.Font.TextMeasurement", []() {
    g = {};
    testTextMeasurement();
    return g.failures;
  });
  registry.add("Illumo.Font.FallbackOnInvalidPath", []() {
    g = {};
    testFontFallbackOnInvalidPath();
    return g.failures;
  });
  registry.add("Illumo.Font.TextureEnrollment", []() {
    g = {};
    testFontTextureEnrollment();
    return g.failures;
  });
  registry.add("Illumo.Font.GameVisualRendering", []() {
    g = {};
    testFontGameVisualRendering();
    return g.failures;
  });
}
