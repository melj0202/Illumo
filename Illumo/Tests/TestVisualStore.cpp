#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/Primitives/VisualStore.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestRegistry.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

static bool
check(bool condition, const char* message)
{
  if (!condition) {
    std::printf("FAILED: %s\n", message);
  }
  return condition;
}

// What one frame queued: vertex uploads by order, and every other token
// type (executed lists flattened) with draw ranges.
struct FrameCapture
{
  std::vector<std::vector<unsigned char>> uploads;
  std::vector<CommandType> types;
  std::vector<unsigned int> draws;
  size_t lists = 0;
};

static FrameCapture
capture(const MockBackend& mock)
{
  FrameCapture result;
  for (size_t index = 0; index < mock.getLastNonEmptySubmittedCount();
       ++index) {
    const RenderCommand& command = mock.getLastNonEmptySubmitted(index);
    if (command.commandType == CommandType::ExecuteList) {
      result.lists += 1;
    } else if (command.commandType == CommandType::UpdateBuffer) {
      const unsigned char* bytes =
        static_cast<const unsigned char*>(command.updateBuffer.data);
      result.uploads.emplace_back(bytes,
                                  bytes + command.updateBuffer.sizeBytes);
    } else {
      result.types.push_back(command.commandType);
      if (command.commandType == CommandType::DrawIndexed) {
        result.draws.push_back(command.drawIndexed.elementCount);
        result.draws.push_back(command.drawIndexed.firstIndex);
      }
    }
  }
  return result;
}

struct StoreFixture
{
  HeadlessRenderFixture render{ 640, 480 };
  VisualStore store;
  TextureHandle texture{};

  StoreFixture()
  {
    const unsigned char pixels[4] = { 255, 255, 255, 255 };
    texture = render.renderer.enrollTexture(pixels, 1, 1, 4);
    store.setRenderer(&render.renderer);
    store.setWindow(&render.window);
    store.setCamera(&render.camera);
  }
  ~StoreFixture() { store.releaseResources(); }

  FrameCapture frame(VisualId id)
  {
    // An empty frame must not read back the previous one.
    render.mock.resetCounters();
    render.renderer.BeginFrame();
    store.append(&render.renderer, id);
    render.renderer.EndFrame();
    return capture(render.mock);
  }
};

// Adds one of each item kind the store carries (text needs a loaded font,
// which headless tests don't have).
static void
addSample(GameVisual& visual, TextureHandle texture)
{
  visual.addFilledRect(10.0f, 10.0f, 40.0f, 20.0f, ColorRgba{ 200, 0, 0, 255 });
  visual.addOutlineRect(
    60.0f, 10.0f, 30.0f, 30.0f, ColorRgba{ 0, 200, 0, 255 }, 2.0f);
  visual.addLine(0.0f, 0.0f, 100.0f, 50.0f, ColorRgba{ 0, 0, 200, 255 }, 3.0f);
  visual.addSprite(texture, 20.0f, 60.0f, 16.0f, 16.0f);
  visual.addFilledEllipse(100.0f, 100.0f, 30.0f, 20.0f, ColorRgba{});
  visual.addFilledTriangle(
    0.0f, 100.0f, 20.0f, 140.0f, 40.0f, 100.0f, ColorRgba{ 9, 9, 9, 255 });
  visual.addGradientRect(150.0f,
                         20.0f,
                         40.0f,
                         40.0f,
                         ColorRgba{ 255, 0, 0, 255 },
                         ColorRgba{ 0, 255, 0, 255 },
                         ColorRgba{ 0, 0, 255, 255 },
                         ColorRgba{ 255, 255, 255, 128 });
  visual.getShape(0)->drawOrder = 5;
}

// The items of `visual` in insertion order, as store records.
static std::vector<GameVisualItem>
itemsOf(GameVisual& visual)
{
  std::vector<GameVisualItem> items;
  size_t shape = 0;
  size_t sprite = 0;
  // addSample order: shape x3, sprite, shape x3.
  for (int index = 0; index < 7; ++index) {
    if (index == 3) {
      items.emplace_back(*visual.getSprite(sprite++));
    } else {
      items.emplace_back(*visual.getShape(shape++));
    }
  }
  return items;
}

static int
recordsMatchNativeOutput()
{
  StoreFixture fixture;
  GameVisual native;
  native.setWindow(&fixture.render.window);
  native.prepare(&fixture.render.renderer);
  addSample(native, fixture.texture);
  fixture.render.renderer.BeginFrame();
  native.AppendCommands(&fixture.render.renderer);
  fixture.render.renderer.EndFrame();
  const FrameCapture expected = capture(fixture.render.mock);

  bool ok = check(fixture.store.create(1) == VisualStore::Result::Ok,
                  "create a visual");
  const std::vector<GameVisualItem> items = itemsOf(native);
  for (size_t index = 0; index < items.size(); ++index) {
    ok = check(fixture.store.setItem(1, index, items[index]) ==
                 VisualStore::Result::Ok,
               "append items by index") &&
         ok;
  }
  const FrameCapture stored = fixture.frame(1);
  ok = check(expected.uploads.size() == 2 && stored.uploads == expected.uploads,
             "stored items tessellate to the same vertex bytes") &&
       ok;
  ok = check(stored.types == expected.types && stored.draws == expected.draws,
             "the recorded list holds the native draw tokens") &&
       ok;
  ok = check(stored.lists == 1 && expected.lists == 0,
             "the store draws through one executed list") &&
       ok;
  return ok ? 0 : 1;
}

static int
recordsOnlyOnChange()
{
  StoreFixture fixture;
  VisualStore& store = fixture.store;
  store.create(7);
  store.setItem(7, 0, ShapePrimitive{});
  ShapePrimitive rect;
  rect.kind = ShapeKind::FilledRect;
  rect.rect = { 5.0f, 5.0f, 50.0f, 50.0f };
  rect.color = ColorRgba{ 10, 20, 30, 255 };
  store.setItem(7, 0, rect);
  fixture.frame(7);
  bool ok = check(store.stats().recordings == 1, "first frame records");

  const FrameCapture steady = fixture.frame(7);
  ok = check(store.stats().recordings == 1 && store.stats().replays == 1 &&
               steady.uploads.empty() && steady.lists == 1,
             "an unchanged frame replays without uploads") &&
       ok;

  VisualProperties properties = *store.properties(7);
  store.setProperties(7, properties);
  fixture.frame(7);
  ok = check(store.stats().recordings == 1,
             "setting identical properties keeps the recording") &&
       ok;

  rect.color = ColorRgba{ 40, 50, 60, 255 };
  store.setItem(7, 0, rect);
  const FrameCapture edited = fixture.frame(7);
  ok = check(store.stats().recordings == 2 && edited.uploads.size() == 1,
             "an item edit uploads and records again") &&
       ok;

  properties.clipEnabled = true;
  properties.clipRect = { 0.0f, 0.0f, 20.0f, 20.0f };
  store.setProperties(7, properties);
  const FrameCapture clipped = fixture.frame(7);
  ok = check(store.stats().recordings == 3 &&
               std::count(clipped.types.begin(),
                          clipped.types.end(),
                          CommandType::SetScissorState) == 2,
             "a clip records its scissor push and pop") &&
       ok;

  fixture.render.window.handleResize(800, 600);
  fixture.frame(7);
  ok = check(store.stats().recordings == 4,
             "a resize changes the projection and records again") &&
       ok;

  properties.visible = false;
  store.setProperties(7, properties);
  const FrameCapture hidden = fixture.frame(7);
  ok = check(hidden.lists == 0 && hidden.types.empty(),
             "a hidden visual queues nothing") &&
       ok;
  return ok ? 0 : 1;
}

static int
editsAndBudgets()
{
  StoreFixture fixture;
  VisualStore::Limits limits;
  limits.visuals = 2;
  limits.itemsPerVisual = 3;
  VisualStore store(limits);
  store.setRenderer(&fixture.render.renderer);
  store.setWindow(&fixture.render.window);
  using Result = VisualStore::Result;
  bool ok = check(
    store.create(1) == Result::Ok && store.create(1) == Result::DuplicateId &&
      store.create(2) == Result::Ok && store.create(3) == Result::OverBudget,
    "visual ids are unique and budgeted");
  ok = check(store.setItem(9, 0, ShapePrimitive{}) == Result::UnknownId &&
               store.setItem(1, 1, ShapePrimitive{}) == Result::BadIndex &&
               store.setItem(1, 3, ShapePrimitive{}) == Result::OverBudget,
             "items append contiguously within the budget") &&
       ok;
  ShapePrimitive shape;
  shape.kind = ShapeKind::FilledRect;
  SpritePrimitive sprite;
  sprite.textureHandle = fixture.texture;
  store.setItem(1, 0, shape);
  store.setItem(1, 1, shape);
  store.setItem(1, 2, shape);
  // Item 1 becomes a sprite and keeps its painter position.
  store.setItem(1, 1, sprite);
  std::vector<GameVisual::PrimitiveRef> order = store.visual(1)->paintOrder();
  ok = check(order.size() == 3 &&
               order[1].kind == GameVisual::PrimitiveKind::Sprite,
             "a kind change keeps the item's place") &&
       ok;
  ok = check(store.removeItems(1, 2, 2) == Result::BadIndex &&
               store.removeItems(1, 0, 1) == Result::Ok &&
               store.visual(1)->itemCount() == 2,
             "remove takes a valid range") &&
       ok;
  order = store.visual(1)->paintOrder();
  ok = check(order.size() == 2 &&
               order[0].kind == GameVisual::PrimitiveKind::Sprite,
             "the remaining items keep their order") &&
       ok;

  // Repeated kind flips compact instead of growing.
  VisualStore big;
  big.create(1);
  for (int round = 0; round < 500; ++round) {
    for (size_t index = 0; index < 4; ++index) {
      if (round % 2 == 0) {
        big.setItem(1, index, shape);
      } else {
        big.setItem(1, index, sprite);
      }
    }
  }
  const GameVisual* flipped = big.visual(1);
  ok = check(flipped->itemCount() == 4 &&
               flipped->shapeCount() + flipped->spriteCount() < 80,
             "replaced primitives are compacted") &&
       ok;
  // Insertion keeps painter order: the new item draws between its
  // neighbours.
  VisualStore ordered;
  ordered.create(1);
  ShapePrimitive first = shape;
  first.rect = { 1.0f, 0.0f, 1.0f, 1.0f };
  ShapePrimitive last = shape;
  last.rect = { 3.0f, 0.0f, 1.0f, 1.0f };
  ShapePrimitive middle = shape;
  middle.rect = { 2.0f, 0.0f, 1.0f, 1.0f };
  ordered.setItem(1, 0, first);
  ordered.setItem(1, 1, last);
  const uint64_t before = ordered.visual(1)->editRevision();
  ok = check(ordered.insertItems(1, 3, 1) == Result::BadIndex &&
               ordered.insertItems(1, 1, 1) == Result::Ok &&
               ordered.setItem(1, 1, middle) == Result::Ok &&
               ordered.visual(1)->editRevision() > before,
             "items insert within the list and bump the edit revision") &&
       ok;
  const std::vector<GameVisual::PrimitiveRef> painted =
    ordered.visual(1)->paintOrder();
  ok = check(painted.size() == 3 &&
               ordered.visual(1)->getShape(painted[0].index)->rect.x == 1.0f &&
               ordered.visual(1)->getShape(painted[1].index)->rect.x == 2.0f &&
               ordered.visual(1)->getShape(painted[2].index)->rect.x == 3.0f,
             "an inserted item paints between its neighbours") &&
       ok;
  ordered.releaseResources();

  ok = check(store.clearItems(1) == Result::Ok &&
               store.visual(1)->itemCount() == 0 &&
               store.destroy(1) == Result::Ok &&
               store.destroy(1) == Result::UnknownId,
             "clear and destroy") &&
       ok;
  store.releaseResources();
  return ok ? 0 : 1;
}

static int
opacityScalesAlpha()
{
  StoreFixture fixture;
  VisualStore& store = fixture.store;
  store.create(1);
  ShapePrimitive rect;
  rect.kind = ShapeKind::FilledRect;
  rect.rect = { 0.0f, 0.0f, 10.0f, 10.0f };
  rect.color = ColorRgba{ 100, 100, 100, 200 };
  store.setItem(1, 0, rect);
  const FrameCapture opaque = fixture.frame(1);
  VisualProperties properties;
  properties.opacity = 0.5f;
  store.setProperties(1, properties);
  const FrameCapture half = fixture.frame(1);
  // Shape vertex: 3 floats then RGBA bytes.
  const size_t alpha = sizeof(float) * 3 + 3;
  bool ok =
    check(opaque.uploads.size() == 1 && half.uploads.size() == 1 &&
            opaque.uploads[0][alpha] == 200 && half.uploads[0][alpha] == 100,
          "opacity halves vertex alpha");
  properties.opacity = 1.0f;
  store.setProperties(1, properties);
  const FrameCapture restored = fixture.frame(1);
  ok = check(restored.uploads.size() == 1 &&
               restored.uploads[0] == opaque.uploads[0],
             "opacity 1 restores identical bytes") &&
       ok;
  return ok ? 0 : 1;
}

// 200 unchanged 50-item panels: native re-emission against store replay.
static int
visualStoreBench()
{
  constexpr int kVisuals = 200;
  constexpr int kItems = 50;
  constexpr int kFrames = 20;
  StoreFixture fixture;
  std::vector<std::unique_ptr<GameVisual>> natives;
  for (int visual = 0; visual < kVisuals; ++visual) {
    natives.push_back(std::make_unique<GameVisual>());
    natives.back()->setWindow(&fixture.render.window);
    natives.back()->prepare(&fixture.render.renderer);
    fixture.store.create(static_cast<VisualId>(visual));
    for (int item = 0; item < kItems; ++item) {
      ShapePrimitive rect;
      rect.kind = ShapeKind::FilledRect;
      rect.rect = {
        static_cast<float>(item * 3), static_cast<float>(visual), 2.0f, 2.0f
      };

      natives.back()->setItem(static_cast<size_t>(item), rect);
      fixture.store.setItem(
        static_cast<VisualId>(visual), static_cast<size_t>(item), rect);
    }
  }
  auto median = [&](auto&& body) {
    std::vector<double> samples;
    for (int frame = 0; frame < kFrames; ++frame) {
      const auto start = std::chrono::steady_clock::now();
      fixture.render.renderer.BeginFrame();
      body();
      fixture.render.renderer.EndFrame();
      samples.push_back(std::chrono::duration<double, std::micro>(
                          std::chrono::steady_clock::now() - start)
                          .count());
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
  };
  const double nativeUs = median([&]() {
    for (std::unique_ptr<GameVisual>& visual : natives) {
      visual->AppendCommands(&fixture.render.renderer);
    }
  });
  const size_t nativeTokens =
    fixture.render.mock.getLastNonEmptySubmittedCount();
  const double storeUs = median([&]() {
    for (int visual = 0; visual < kVisuals; ++visual) {
      fixture.store.append(&fixture.render.renderer,
                           static_cast<VisualId>(visual));
    }
  });
  std::printf("VisualStoreBench visuals=%d items=%d native_us=%.1f "
              "native_tokens=%zu store_us=%.1f recordings=%zu\n",
              kVisuals,
              kItems,
              nativeUs,
              nativeTokens,
              storeUs,
              fixture.store.stats().recordings);
  return check(fixture.store.stats().recordings ==
                 static_cast<size_t>(kVisuals),
               "each unchanged visual records once")
           ? 0
           : 1;
}

void
registerVisualStoreTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.VisualStore.RecordsMatchNativeOutput",
               recordsMatchNativeOutput);
  registry.add("Illumo.VisualStore.RecordsOnlyOnChange", recordsOnlyOnChange);
  registry.add("Illumo.VisualStore.EditsAndBudgets", editsAndBudgets);
  registry.add("Illumo.VisualStore.OpacityScalesAlpha", opacityScalesAlpha);
  registry.add("Illumo.VisualStore.Bench", visualStoreBench, 120);
}
