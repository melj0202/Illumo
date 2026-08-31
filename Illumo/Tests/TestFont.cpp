#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Font.h>
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

void
registerFontTests(IllumoTestRegistry& registry)
{
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
