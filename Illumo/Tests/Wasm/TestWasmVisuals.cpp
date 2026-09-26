// Frame schema v7: host-retained visuals and compositions (HostRender).

#include <Illumo/Rendering/Font.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Wasm/WasmFrameRenderer.h>
#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <string>
#include <vector>

static std::vector<std::byte>
encode(const GuestFrame& frame)
{
  GuestWireWriter wire;
  frame.write(wire);
  return wire.take();
}

static GuestVisualOperation
visualOp(GuestVisualOp op, std::uint32_t id)
{
  GuestVisualOperation operation;
  operation.op = op;
  operation.id = id;
  return operation;
}

static GuestVisualOperation
setLayer(std::uint32_t id, GuestLayer layer)
{
  GuestVisualOperation operation = visualOp(GuestVisualOp::Set, id);
  operation.properties.layer = layer;
  return operation;
}

static GuestVisualOperation
shapeItem(std::uint32_t id, std::uint32_t index, float x = 10.0f)
{
  GuestVisualOperation operation = visualOp(GuestVisualOp::ItemSet, id);
  operation.index = index;
  operation.item.kind = GuestItemKind::Shape;
  operation.item.rect = { x, 10.0f, 40.0f, 20.0f };
  operation.item.rgba = 0xff2040c0u;
  return operation;
}

static GuestVisualOperation
spriteItem(std::uint32_t id,
           std::uint32_t index,
           const GuestResourceId& texture)
{
  GuestVisualOperation operation = visualOp(GuestVisualOp::ItemSet, id);
  operation.index = index;
  operation.item.kind = GuestItemKind::Sprite;
  operation.item.rect = { 60.0f, 10.0f, 16.0f, 16.0f };
  operation.item.texture = texture;
  operation.item.flipY = true;
  return operation;
}

static GuestVisualOperation
textItem(std::uint32_t id, std::uint32_t index, const GuestResourceId& font)
{
  GuestVisualOperation operation = visualOp(GuestVisualOp::ItemSet, id);
  operation.index = index;
  operation.item.kind = GuestItemKind::Text;
  operation.item.rect = { 5.0f, 40.0f, 0.0f, 0.0f };
  operation.item.font = font;
  operation.item.sizePt = 14.0f;
  operation.item.text = "Hi";
  return operation;
}

static GuestCompositionEntry
entry(GuestCompositionKind kind, std::uint32_t first, std::uint32_t count = 0)
{
  GuestCompositionEntry value;
  value.kind = kind;
  value.first = first;
  value.count = count;
  return value;
}

static GuestComposition
composition(std::uint32_t target, std::vector<GuestCompositionEntry> entries)
{
  GuestComposition value;
  value.target = target;
  value.entries = std::move(entries);
  return value;
}

static GuestComposition
same(std::uint32_t target)
{
  GuestComposition value;
  value.target = target;
  value.same = true;
  return value;
}

static GuestBatch
shapeBatch(GuestLayer layer = GuestLayer::Ui)
{
  GuestBatch batch;
  batch.style = GuestBatchStyle::Shape;
  batch.layer = layer;
  batch.vertices.resize(3);
  batch.vertices[1].position = { 10.0f, 0.0f, 0.0f };
  batch.vertices[2].position = { 0.0f, 10.0f, 0.0f };
  batch.indices = { 0, 1, 2 };
  return batch;
}

static GuestFrame
visualFrame(std::vector<GuestVisualOperation> operations,
            std::vector<GuestComposition> compositions = {},
            std::vector<GuestBatch> batches = {})
{
  GuestFrame frame;
  frame.width = 640;
  frame.height = 480;
  frame.visualOperations = std::move(operations);
  frame.compositions = std::move(compositions);
  frame.batches = std::move(batches);
  return frame;
}

static bool
visualFrameValidation()
{
  TestCounters counters;
  const GuestResourceId texture{ 700, GuestResourceKind::Texture, 4, 1 };
  const GuestResourceId font{ 700, GuestResourceKind::Texture, 5, 1 };
  GuestVisualOperation properties = setLayer(1, GuestLayer::World);
  properties.properties.worldSpace = true;
  properties.properties.clipped = true;
  properties.properties.clip = { 1.0f, 2.0f, 30.0f, 40.0f };
  properties.properties.opacity = 0.5f;
  properties.properties.visible = false;
  properties.properties.transform.rotation = 0.25f;
  GuestVisualOperation gradient = shapeItem(1, 0);
  gradient.item.shape = 5;
  gradient.item.points[7] = 9.0f;
  gradient.item.vertexColors[3] = 0x80ffffffu;
  gradient.item.drawOrder = -3;
  GuestVisualOperation heavy = textItem(1, 2, font);
  heavy.item.heavyFont = font;
  heavy.item.heavyBlend = 0.25f;
  GuestVisualOperation removal = visualOp(GuestVisualOp::ItemRemove, 1);
  removal.index = 1;
  removal.count = 2;
  const GuestFrame frame =
    visualFrame({ visualOp(GuestVisualOp::Create, 1),
                  properties,
                  gradient,
                  spriteItem(1, 1, texture),
                  heavy,
                  removal,
                  visualOp(GuestVisualOp::ItemsClear, 1),
                  visualOp(GuestVisualOp::Destroy, 1) },
                { composition(0,
                              { entry(GuestCompositionKind::World, 0),
                                entry(GuestCompositionKind::Batches, 0, 2),
                                entry(GuestCompositionKind::Visual, 1) }),
                  same(5) });
  const std::vector<std::byte> bytes = encode(frame);
  GuestFrame decoded;
  const bool read = GuestFrame::read(bytes, decoded);
  const std::vector<GuestVisualOperation>& ops = decoded.visualOperations;
  testTrue(
    counters,
    read && ops.size() == 8 && ops[1].properties.layer == GuestLayer::World &&
      ops[1].properties.worldSpace && ops[1].properties.clipped &&
      !ops[1].properties.visible && ops[1].properties.clip[3] == 40.0f &&
      ops[1].properties.opacity == 0.5f &&
      ops[1].properties.transform.rotation == 0.25f && ops[2].item.shape == 5 &&
      ops[2].item.points[7] == 9.0f &&
      ops[2].item.vertexColors[3] == 0x80ffffffu &&
      ops[2].item.drawOrder == -3 && ops[2].item.rgba == 0xff2040c0u &&
      ops[3].item.kind == GuestItemKind::Sprite &&
      ops[3].item.texture.slot == 4 && ops[3].item.flipY &&
      !ops[3].item.flipX && ops[4].item.text == "Hi" &&
      ops[4].item.heavyFont.slot == 5 && ops[4].item.heavyBlend == 0.25f &&
      ops[4].item.sizePt == 14.0f && ops[5].index == 1 && ops[5].count == 2 &&
      ops[7].op == GuestVisualOp::Destroy,
    "Every visual operation round-trips");
  testTrue(counters,
           read && decoded.compositions.size() == 2 &&
             decoded.compositions[0].entries.size() == 3 &&
             decoded.compositions[0].entries[1].count == 2 &&
             decoded.compositions[0].entries[2].first == 1 &&
             decoded.compositions[0].width == 1280.0f &&
             decoded.compositions[0].height == 720.0f &&
             decoded.compositions[1].same &&
             decoded.compositions[1].target == 5,
           "Compositions round-trip");

  GuestVisualOperation insertion = visualOp(GuestVisualOp::ItemInsert, 4);
  insertion.index = 2;
  insertion.count = 3;
  GuestFrame inserted;
  testTrue(counters,
           GuestFrame::read(encode(visualFrame({ insertion })), inserted) &&
             inserted.visualOperations.size() == 1 &&
             inserted.visualOperations[0].op == GuestVisualOp::ItemInsert &&
             inserted.visualOperations[0].index == 2 &&
             inserted.visualOperations[0].count == 3,
           "ItemInsert round-trips");

  bool truncations = true;
  for (std::size_t size = 0; size < bytes.size(); ++size) {
    GuestFrame ignored;
    truncations =
      truncations && !GuestFrame::read(std::span(bytes).first(size), ignored);
  }
  testTrue(counters, truncations, "Every truncated v7 frame is rejected");

  const float nan = std::numeric_limits<float>::quiet_NaN();
  GuestVisualOperation unknown = visualOp(static_cast<GuestVisualOp>(8), 1);
  GuestVisualOperation zeroId = visualOp(GuestVisualOp::Create, 0);
  GuestVisualOperation badShape = shapeItem(1, 0);
  badShape.item.shape = 6;
  GuestVisualOperation nanShape = shapeItem(1, 0);
  nanShape.item.rect[0] = nan;
  GuestVisualOperation meshSprite = spriteItem(1, 0, texture);
  meshSprite.item.texture.kind = GuestResourceKind::Mesh;
  GuestVisualOperation noFont = textItem(1, 0, GuestResourceId{});
  GuestVisualOperation tinyText = textItem(1, 0, font);
  tinyText.item.sizePt = 0.0f;
  GuestVisualOperation overBlend = textItem(1, 0, font);
  overBlend.item.heavyFont = font;
  overBlend.item.heavyBlend = 2.0f;
  GuestVisualOperation opaque = setLayer(1, GuestLayer::Ui);
  opaque.properties.opacity = 1.5f;
  GuestVisualOperation negativeClip = setLayer(1, GuestLayer::Ui);
  negativeClip.properties.clip[2] = -1.0f;
  GuestVisualOperation emptyRemoval = visualOp(GuestVisualOp::ItemRemove, 1);
  GuestVisualOperation emptyInsert = visualOp(GuestVisualOp::ItemInsert, 1);
  bool denied = true;
  for (const GuestVisualOperation& invalid : { unknown,
                                               zeroId,
                                               badShape,
                                               nanShape,
                                               meshSprite,
                                               noFont,
                                               tinyText,
                                               overBlend,
                                               opaque,
                                               negativeClip,
                                               emptyRemoval,
                                               emptyInsert }) {
    GuestFrame ignored;
    denied =
      denied && !GuestFrame::read(encode(visualFrame({ invalid })), ignored);
  }
  testTrue(counters,
           denied,
           "Unknown operations, zero ids, bad shapes, non-finite values, "
           "non-texture sprites and fonts, and out-of-range properties are "
           "denied");

  bool deniedCompositions = true;
  for (const std::vector<GuestComposition>& invalid :
       std::vector<std::vector<GuestComposition>>{
         { composition(0, { entry(static_cast<GuestCompositionKind>(4), 1) }) },
         { composition(0, { entry(GuestCompositionKind::Visual, 0) }) },
         { composition(0, { entry(GuestCompositionKind::Batches, 0, 0) }) },
         { composition(3, { entry(GuestCompositionKind::World, 0) }) },
         { same(0), same(0) },
         { [] {
           GuestComposition flat =
             composition(0, { entry(GuestCompositionKind::Visual, 1) });
           flat.height = 0.0f;
           return flat;
         }() } }) {
    GuestFrame ignored;
    deniedCompositions =
      deniedCompositions &&
      !GuestFrame::read(encode(visualFrame({}, invalid)), ignored);
  }
  // One composition past the limit, which `write` refuses to encode: append
  // a tenth `same` record and bump the trailing section's count.
  std::vector<GuestComposition> most;
  for (std::uint32_t target = 0; target <= GuestFrame::MaximumSurfaces;
       ++target) {
    most.push_back(same(target));
  }
  std::vector<std::byte> tooMany = encode(visualFrame({}, most));
  const std::size_t countAt = tooMany.size() - most.size() * 8 - 4;
  tooMany[countAt] = static_cast<std::byte>(most.size() + 1);
  const std::array<std::byte, 8> extra{ std::byte{ 42 }, {}, {}, {},
                                        std::byte{ 1 },  {}, {}, {} };
  tooMany.insert(tooMany.end(), extra.begin(), extra.end());
  GuestFrame most9;
  deniedCompositions = deniedCompositions &&
                       GuestFrame::read(encode(visualFrame({}, most)), most9) &&
                       !GuestFrame::read(tooMany, most9);
  testTrue(counters,
           deniedCompositions,
           "Unknown entries, zero visual ids, empty ranges, a world on a "
           "surface, duplicate targets, a zero size and too many compositions "
           "are denied");

  GuestFrame ignored;
  GuestFrameLimits oneOperation;
  oneOperation.worldOperations = 1;
  GuestFrame mixed = visualFrame({ visualOp(GuestVisualOp::Create, 1) });
  GuestWorldOperation material;
  material.op = GuestWorldOp::MaterialCreate;
  material.id = 1;
  mixed.worldOperations.push_back(material);
  GuestFrameLimits oneTextByte;
  oneTextByte.textBytes = 1;
  const GuestFrame text = visualFrame({ textItem(1, 0, font) });
  GuestFrameLimits oneEntry;
  oneEntry.compositionEntries = 1;
  const GuestFrame twoEntries =
    visualFrame({},
                { composition(0,
                              { entry(GuestCompositionKind::Visual, 1),
                                entry(GuestCompositionKind::Visual, 2) }) });
  testTrue(counters,
           !GuestFrame::read(encode(mixed), ignored, oneOperation) &&
             mixed.exceededLimit(oneOperation) != nullptr &&
             !GuestFrame::read(encode(text), ignored, oneTextByte) &&
             text.exceededLimit(oneTextByte) != nullptr &&
             !GuestFrame::read(encode(twoEntries), ignored, oneEntry) &&
             twoEntries.exceededLimit(oneEntry) != nullptr &&
             mixed.exceededLimit() == nullptr &&
             text.exceededLimit() == nullptr,
           "Shared operation, text and entry quotas are enforced and reported "
           "to guests");

  // Version 6: no visual or composition sections.
  std::vector<std::byte> version6 = encode(visualFrame({}));
  version6[4] = std::byte{ 6 };
  version6.resize(version6.size() - 8);
  testTrue(counters,
           GuestFrame::read(version6, ignored) &&
             ignored.visualOperations.empty() && ignored.compositions.empty(),
           "Version 6 frames remain accepted without visuals");
  return counters.failures == 0;
}

// Positions of token types in the last submission, list contents flattened.
static std::vector<CommandType>
submittedTypes(const MockBackend& mock)
{
  std::vector<CommandType> types;
  for (std::size_t index = 0; index < mock.getLastNonEmptySubmittedCount();
       ++index) {
    types.push_back(mock.getLastNonEmptySubmittedType(index));
  }
  return types;
}

static std::size_t
countOf(const std::vector<CommandType>& types, CommandType type)
{
  return static_cast<std::size_t>(std::count(types.begin(), types.end(), type));
}

static bool
visualOperations()
{
  TestCounters counters;
  NullRenderWindow window(640, 480);
  EnvVars env;
  env.setVar("WinX", 640);
  env.setVar("WinY", 480);
  Camera camera(glm::vec2(0, 0), 1, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  renderer.ensureBuiltinStyles();
  WasmFrameRenderer bridge(renderer, 700);

  const std::array<std::byte, 64> pixels{};
  const GuestResourceId texture = bridge.createTexture(pixels, 4, 4, 4, false);
  const GuestResourceId font = bridge.createFontAtlas(Font::createFallback(16));
  testTrue(counters,
           texture.owner != 0 && font.owner != 0,
           "A texture and a font atlas are created");

  const std::function<std::vector<CommandType>()> render = [&]() {
    Scene scene(&window, &camera);
    bridge.dispatch(scene);
    mock.resetCounters();
    renderer.BeginFrame();
    renderer.RenderScene(&scene, &camera);
    renderer.EndFrame();
    return submittedTypes(mock);
  };

  testTrue(
    counters,
    bridge.accept(encode(visualFrame(
      { visualOp(GuestVisualOp::Create, 1),
        setLayer(1, GuestLayer::Ui),
        shapeItem(1, 0),
        spriteItem(1, 1, texture),
        textItem(1, 2, font) },
      { composition(0, { entry(GuestCompositionKind::Visual, 1) }) }))) &&
      bridge.counters().visuals == 1 && bridge.counters().visualOperations == 5,
    "A visual and its items are created on the host");
  std::vector<CommandType> types = render();
  testTrue(counters,
           countOf(types, CommandType::ExecuteList) == 1 &&
             countOf(types, CommandType::DrawIndexed) >= 2 &&
             renderer.frameError().empty(),
           "The visual draws from one executed list");
  testTrue(counters,
           bridge.accept(encode(visualFrame({}, { same(0) }))) &&
             countOf(render(), CommandType::ExecuteList) == 1 &&
             renderer.frameError().empty(),
           "A `same` composition keeps drawing the visual");
  testTrue(counters,
           bridge.accept(encode(visualFrame({}))) &&
             countOf(render(), CommandType::ExecuteList) == 0,
           "A frame without a composition draws no visuals");

  // Painter order: batch 0, the visual, batch 1.
  testTrue(counters,
           bridge.accept(encode(visualFrame(
             {},
             { composition(0,
                           { entry(GuestCompositionKind::Batches, 0, 1),
                             entry(GuestCompositionKind::Visual, 1),
                             entry(GuestCompositionKind::Batches, 1, 1) }) },
             { shapeBatch(), shapeBatch() }))),
           "Batches and visuals compose in one frame");
  types = render();
  std::vector<std::size_t> draws;
  std::size_t list = types.size();
  for (std::size_t index = 0; index < types.size(); ++index) {
    if (types[index] == CommandType::DrawIndexed) {
      draws.push_back(index);
    } else if (types[index] == CommandType::ExecuteList) {
      list = index;
    }
  }
  testTrue(counters,
           draws.size() >= 4 && list > draws.front() && list < draws.back() &&
             renderer.frameError().empty(),
           "The visual draws between the two batches");

  const std::uint64_t visualsBefore = bridge.counters().visuals;
  bool denied = true;
  for (const GuestFrame& invalid :
       { visualFrame(
           {},
           { composition(0, { entry(GuestCompositionKind::Batches, 0, 1) }) },
           { shapeBatch(), shapeBatch() }),
         visualFrame(
           { setLayer(1, GuestLayer::World) },
           { composition(0,
                         { entry(GuestCompositionKind::Batches, 0, 1),
                           entry(GuestCompositionKind::Visual, 1) }) },
           { shapeBatch() }),
         visualFrame(
           {}, { composition(0, { entry(GuestCompositionKind::Visual, 9) }) }),
         visualFrame(
           {},
           { composition(0,
                         { entry(GuestCompositionKind::Visual, 1),
                           entry(GuestCompositionKind::World, 0) }) }),
         visualFrame(
           {},
           { composition(0,
                         { entry(GuestCompositionKind::World, 0),
                           entry(GuestCompositionKind::World, 0) }) }),
         visualFrame(
           {}, { composition(9, { entry(GuestCompositionKind::Visual, 1) }) }),
         visualFrame({}, { same(3) }),
         visualFrame({ shapeItem(1, 5) }),
         visualFrame({ spriteItem(
           1, 3, GuestResourceId{ 700, GuestResourceKind::Texture, 99, 1 }) }),
         visualFrame({ textItem(1, 3, texture) }),
         visualFrame({ visualOp(GuestVisualOp::Create, 1) }),
         visualFrame({ visualOp(GuestVisualOp::Destroy, 2) }),
         visualFrame({ visualOp(GuestVisualOp::Create, 2), shapeItem(2, 0), [] {
                        GuestVisualOperation removal =
                          visualOp(GuestVisualOp::ItemRemove, 2);
                        removal.index = 0;
                        removal.count = 2;
                        return removal;
                      }() }) }) {
    denied = denied && !bridge.accept(encode(invalid));
  }
  testTrue(counters,
           denied && bridge.counters().visuals == visualsBefore,
           "Uncovered or out-of-order batches, layer order, unknown visuals "
           "and targets, a second world, item gaps, missing textures, "
           "non-font text and bad ids are denied");
  testTrue(counters,
           bridge.accept(encode(visualFrame(
             { visualOp(GuestVisualOp::Create, 2), shapeItem(2, 0) },
             { composition(0,
                           { entry(GuestCompositionKind::World, 0),
                             entry(GuestCompositionKind::Visual, 2),
                             entry(GuestCompositionKind::Visual, 1) }) }))) &&
             countOf(render(), CommandType::ExecuteList) == 2 &&
             renderer.frameError().empty(),
           "After rejections, the next frame applies and draws both visuals");

  // Insertion before existing items: within the list, once.
  const auto insertAt = [](std::uint32_t index, std::uint32_t count) {
    GuestVisualOperation operation = visualOp(GuestVisualOp::ItemInsert, 2);
    operation.index = index;
    operation.count = count;
    return operation;
  };
  testTrue(counters,
           !bridge.accept(encode(visualFrame({ insertAt(2, 1) }))) &&
             bridge.accept(encode(visualFrame(
               { insertAt(0, 2), shapeItem(2, 0, 5.0f), shapeItem(2, 1, 6.0f) },
               { composition(0, { entry(GuestCompositionKind::Visual, 2) }) }))) &&
             countOf(render(), CommandType::ExecuteList) == 1 &&
             renderer.frameError().empty(),
           "Items insert before existing ones, never past the end");

  // Surfaces: the composition decides when a surface must be replayed.
  GuestSurfaceFrame surface;
  surface.surface = 4;
  surface.width = 200;
  surface.height = 100;
  surface.revision = 1;
  surface.batches = { shapeBatch() };
  GuestFrame first =
    visualFrame({},
                { composition(4,
                              { entry(GuestCompositionKind::Batches, 0, 1),
                                entry(GuestCompositionKind::Visual, 2) }) });
  first.surfaces = { surface };
  GuestSurfaceFrame unchanged = surface;
  unchanged.same = true;
  unchanged.batches.clear();
  GuestFrame steady = visualFrame({}, { same(4) });
  steady.surfaces = { unchanged };
  GuestFrame edited = visualFrame({ shapeItem(2, 0, 30.0f) }, { same(4) });
  edited.surfaces = { unchanged };
  const auto presented = [&]() {
    const std::vector<WasmSurfaceContent> contents = bridge.surfaces();
    return contents.size() == 1 ? contents[0].revision : 0;
  };
  const bool firstAccepted = bridge.accept(encode(first));
  const std::uint64_t afterFirst = presented();
  const bool steadyAccepted = bridge.accept(encode(steady));
  const std::uint64_t afterSteady = presented();
  const bool editAccepted = bridge.accept(encode(edited));
  const std::uint64_t afterEdit = presented();
  testTrue(counters,
           firstAccepted && steadyAccepted && editAccepted && afterFirst != 0 &&
             afterSteady == afterFirst && afterEdit != afterSteady,
           "A surface replays for new content and for listed visual edits "
           "only");
  DrawableBase* drawable = bridge.surfaceDrawable(4);
  mock.resetCounters();
  renderer.BeginFrame();
  const bool drawn = drawable != nullptr && drawable->AppendCommands(&renderer);
  renderer.EndFrame();
  types = submittedTypes(mock);
  testTrue(counters,
           drawn && countOf(types, CommandType::ExecuteList) == 1 &&
             countOf(types, CommandType::DrawIndexed) >= 2,
           "A surface replays its batches and visuals");

  bridge.retire();
  testTrue(counters,
           countOf(render(), CommandType::ExecuteList) == 0,
           "Retirement drops every visual at once");
  return counters.failures == 0;
}

bool
runWasmVisualTest(const std::string& name)
{
  if (name == "VisualFrameValidation") {
    return visualFrameValidation();
  }
  if (name == "VisualOperations") {
    return visualOperations();
  }
  return false;
}
