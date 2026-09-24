#include <Illumo/Content/IlscCodec.h>
#include <Illumo/Content/SceneInstance.h>
#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/AssetSource.h>
#include <Illumo/Rendering/Primitives/MeshVisual.h>
#include <Illumo/Rendering/Primitives/SkyboxVisual.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>

// Serves in-memory bytes by virtual path, like the guest package cache.
class MemoryAssetSource : public IAssetSource
{
public:
  std::map<std::string, std::vector<unsigned char>> files;
  std::string canonical(const std::string& path) const override { return path; }
  bool read(const std::string& canonical,
            std::vector<unsigned char>& bytes) const override
  {
    const std::map<std::string, std::vector<unsigned char>>::const_iterator
      found = files.find(canonical);
    if (found == files.end()) {
      return false;
    }
    bytes = found->second;
    return true;
  }
  std::int64_t stamp(const std::string& canonical) const override
  {
    (void)canonical;
    return 0;
  }
};

// Uncompressed 32-bit top-left TGA, which stb_image decodes.
// The grid row of a laid-out index (integer division, kept explicit).
static int
rowOf(int index, int columns)
{
  return index / columns;
}

static std::vector<unsigned char>
solidTga(int width, int height, unsigned char red)
{
  std::vector<unsigned char> bytes(18, 0);
  bytes[2] = 2;
  bytes[12] = static_cast<unsigned char>(width & 0xff);
  bytes[13] = static_cast<unsigned char>(width >> 8);
  bytes[14] = static_cast<unsigned char>(height & 0xff);
  bytes[15] = static_cast<unsigned char>(height >> 8);
  bytes[16] = 32;
  bytes[17] = 0x28;
  for (int pixel = 0; pixel < width * height; ++pixel) {
    bytes.push_back(40);
    bytes.push_back(80);
    bytes.push_back(red);
    bytes.push_back(255);
  }
  return bytes;
}

static std::vector<unsigned char>
text(const char* value)
{
  const std::string content(value);
  return std::vector<unsigned char>(content.begin(), content.end());
}

static SceneNode
primitiveNode(const std::string& id,
              const std::string& parent,
              ScenePrimitiveShape shape,
              const Vector3& position)
{
  SceneNode node;
  node.id = id;
  node.parentId = parent;
  node.name = id;
  node.transform.position = position;
  SceneComponent component;
  component.value = ScenePrimitive{ shape,
                                    Vector3(0.5f, 0.5f, 0.5f),
                                    ColorRgba{ 200, 100, 50, 255 } };
  node.components.push_back(component);
  return node;
}

static size_t
renderFrame(HeadlessRenderFixture& fixture, SceneInstance& instance)
{
  Scene scene(&fixture.window, &fixture.camera);
  if (instance.skybox() != nullptr) {
    scene.AddDrawable(instance.skybox());
  }
  scene.AddDrawable(&instance.drawable());
  instance.update();
  fixture.mock.resetCounters();
  fixture.renderer.BeginFrame();
  fixture.renderer.RenderScene(&scene, &fixture.camera);
  fixture.renderer.EndFrame();
  size_t draws = 0;
  for (size_t frame = 0; frame < fixture.mock.getSubmittedFrameCount();
       ++frame) {
    for (const RenderCommand& command : fixture.mock.getSubmittedFrame(frame)) {
      draws += command.commandType == CommandType::DrawIndexed ? 1 : 0;
    }
  }
  return draws;
}

static int
testInstancePrimitives()
{
  TestCounters counters;
  HeadlessRenderFixture fixture(640, 480);
  fixture.mock.Initialize();
  AssetManager assets(&fixture.renderer, false);
  SceneDocument document;
  document.worldMode = SceneWorldMode::World3D;
  for (int index = 0; index < 60; ++index) {
    const ScenePrimitiveShape shape =
      index % 2 == 0 ? ScenePrimitiveShape::Cube : ScenePrimitiveShape::Rect;
    document.nodes.push_back(
      primitiveNode("n" + std::to_string(index),
                    "",
                    shape,
                    Vector3(static_cast<float>(index % 10) - 5.0f,
                            static_cast<float>(rowOf(index, 10)) - 3.0f,
                            0.0f)));
  }
  // Preorder: the child follows its parent directly.
  document.nodes.insert(
    document.nodes.begin() + 1,
    primitiveNode(
      "wire", "n0", ScenePrimitiveShape::WireSphere, Vector3(0.0f)));
  SceneInstance instance(&assets);
  instance.setRenderer(&fixture.renderer);
  const size_t createsBefore = fixture.mock.getCreateCount();
  std::string error;
  testTrue(
    counters, instance.load(document, "/app", error), "a valid document loads");
  size_t enrolled = 0;
  for (size_t index = createsBefore; index < fixture.mock.getCreateCount();
       ++index) {
    enrolled += fixture.mock.getCreate(index).kind ==
                    MockBackend::CreateRecord::Kind::Mesh
                  ? 1
                  : 0;
  }
  std::printf("meshes enrolled by load: %zu\n", enrolled);
  testTrue(counters,
           enrolled <= 3,
           "solid primitives share one enrolled mesh per shape");
  testEqSize(counters, instance.nodeCount(), 61, "every node is live");
  testEqStr(counters,
            IlscCodec::encode(instance.document()),
            IlscCodec::encode(document),
            "the live document matches the loaded one");
  fixture.camera.setProjectionType(ProjectionType::Perspective);
  fixture.camera.setPerspective(60.0f, 0.1f, 100.0f);
  fixture.camera.lookAt(
    Vector3(0.0f, 0.0f, 20.0f), Vector3(0.0f), Vector3(0.0f, 1.0f, 0.0f));
  const size_t draws = renderFrame(fixture, instance);
  testTrue(counters, draws >= 61, "every primitive emits a draw");
  testTrue(counters,
           instance.graph().getAttachmentCount(instance.handleOf("wire")) == 1,
           "wire shapes draw through their own visual");
  testTrue(counters, instance.warnings().empty(), "no warnings");

  SceneInstance unmanaged;
  testTrue(counters,
           unmanaged.load(document, "/app", error) &&
             unmanaged.nodeCount() == 61,
           "without an asset manager primitives still build");
  return counters.failures;
}

static int
testInstanceEnvironmentLight()
{
  TestCounters counters;
  SceneDocument document;
  document.worldMode = SceneWorldMode::World3D;
  document.environment.sun.direction = Vector3(0.0f, -2.0f, 0.0f);
  document.environment.sun.color = Vector3(1.0f, 0.5f, 0.25f);
  document.environment.sun.intensity = 2.0f;
  document.nodes.push_back(
    primitiveNode("box", "", ScenePrimitiveShape::Cube, Vector3(0.0f)));
  SceneInstance instance;
  std::string error;
  testTrue(counters, instance.load(document, "/app", error), "loads");
  const SceneLighting& sun = instance.lighting();
  testTrue(counters,
           sun.lit && sun.sourceId.empty() &&
             glm::length(sun.towardLight - Vector3(0.0f, 1.0f, 0.0f)) < 1e-5f &&
             sun.color == Vector3(2.0f, 1.0f, 0.5f),
           "the environment sun lights the scene");

  SceneNode lamp;
  lamp.id = "lamp";
  lamp.transform.rotation =
    glm::angleAxis(glm::radians(90.0f), Vector3(0.0f, 0.0f, 1.0f));
  SceneComponent light;
  light.value = SceneLight{ Vector3(0.5f, 0.5f, 1.0f), 1.0f, false };
  lamp.components.push_back(light);
  testTrue(counters, instance.insertNode(lamp, "", error), "light inserted");
  instance.update();
  const SceneLighting& lit = instance.lighting();
  testTrue(counters,
           lit.sourceId == "lamp" &&
             glm::length(lit.towardLight - Vector3(-1.0f, 0.0f, 0.0f)) <
               1e-5f &&
             !lit.shadows,
           "a light component replaces the sun and follows its rotation");
  testTrue(counters, instance.setEnabled("lamp", false), "light disabled");
  instance.update();
  testTrue(counters,
           instance.lighting().sourceId.empty(),
           "a disabled light falls back to the sun");
  SceneEnvironment environment = instance.document().environment;
  environment.hasSun = false;
  testTrue(
    counters, instance.setEnvironment(environment, error), "sun removed");
  testTrue(counters, !instance.lighting().lit, "no sun and no light is unlit");
  instance.setWorldMode(SceneWorldMode::World2D);
  testTrue(counters, !instance.lighting().lit, "2D scenes are unlit");
  return counters.failures;
}

static int
testInstanceSkyboxAndAssets()
{
  TestCounters counters;
  HeadlessRenderFixture fixture(640, 480);
  fixture.mock.Initialize();
  MemoryAssetSource source;
  source.files["/packages/forest/sky.tga"] = solidTga(64, 48, 200);
  source.files["/packages/forest/leaf.tga"] = solidTga(8, 8, 20);
  source.files["/packages/forest/meshes/tri.obj"] =
    text("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
  AssetManager assets(&fixture.renderer, false, &source);
  SceneDocument document;
  document.worldMode = SceneWorldMode::World3D;
  SceneAsset sky;
  sky.id = "sky";
  sky.type = SceneAssetType::CubemapCross;
  sky.path = "sky.tga";
  SceneAsset leaf;
  leaf.id = "leaf";
  leaf.type = SceneAssetType::Atlas;
  leaf.path = "leaf.tga";
  leaf.columns = 2;
  leaf.rows = 2;
  SceneAsset mesh;
  mesh.id = "tri";
  mesh.type = SceneAssetType::Mesh;
  mesh.path = "meshes/tri.obj";
  SceneAsset missing;
  missing.id = "missing";
  missing.type = SceneAssetType::Mesh;
  missing.path = "meshes/none.obj";
  document.assets = { sky, leaf, mesh, missing };
  document.environment.skybox = "sky";

  SceneNode meshNode;
  meshNode.id = "m";
  SceneComponent meshComponent;
  meshComponent.value = SceneMeshRenderer{ "tri", ColorRgba{}, true };
  meshNode.components.push_back(meshComponent);
  SceneNode spriteNode;
  spriteNode.id = "s";
  SceneSprite sprite;
  sprite.texture = "leaf";
  sprite.hasCell = true;
  sprite.column = 1;
  sprite.row = 1;
  SceneComponent spriteComponent;
  spriteComponent.value = sprite;
  spriteNode.components.push_back(spriteComponent);
  SceneNode brokenNode;
  brokenNode.id = "broken";
  SceneComponent brokenComponent;
  brokenComponent.value = SceneMeshRenderer{ "missing", ColorRgba{}, true };
  brokenNode.components.push_back(brokenComponent);
  document.nodes = { meshNode, spriteNode, brokenNode };

  SceneInstance instance(&assets);
  instance.setRenderer(&fixture.renderer);
  std::string error;
  testTrue(counters,
           instance.load(document, "/packages/forest", error),
           "scene with assets loads");
  testTrue(counters,
           instance.skybox() != nullptr &&
             instance.skybox()->getCubemap().isValid(),
           "the environment skybox resolves against the package root");
  testEqSize(counters, instance.warnings().size(), 1, "one asset failed");
  testTrue(counters,
           !instance.warnings().empty() &&
             instance.warnings()[0].find("missing") != std::string::npos,
           "the warning names the failed asset");
  AxisAlignedBounds3 bounds;
  testTrue(counters,
           instance.localBounds("m", &bounds) &&
             std::fabs(bounds.maximum.x - 1.0f) < 1e-5f,
           "the mesh component draws the loaded mesh");
  testTrue(counters,
           instance.localBounds("broken", &bounds) &&
             std::fabs(bounds.maximum.x - 0.5f) < 1e-5f,
           "a failed mesh draws a placeholder cube");
  testTrue(counters,
           instance.graph().getAttachmentCount(instance.handleOf("s")) == 1,
           "the sprite has a visual");
  fixture.camera.setProjectionType(ProjectionType::Perspective);
  fixture.camera.setPerspective(60.0f, 0.1f, 100.0f);
  fixture.camera.lookAt(
    Vector3(0.0f, 0.0f, 5.0f), Vector3(0.0f), Vector3(0.0f, 1.0f, 0.0f));
  testTrue(counters, renderFrame(fixture, instance) >= 4, "everything draws");

  SceneInstance noManager;
  testTrue(counters,
           noManager.load(document, "/packages/forest", error) &&
             noManager.skybox() == nullptr && noManager.warnings().size() >= 3,
           "without an asset manager assets become placeholders with warnings");
  return counters.failures;
}

static int
testInstanceIncrementalEdit()
{
  TestCounters counters;
  SceneDocument document;
  document.nodes.push_back(
    primitiveNode("a", "", ScenePrimitiveShape::Cube, Vector3(0.0f)));
  document.nodes.push_back(
    primitiveNode("b", "a", ScenePrimitiveShape::Rect, Vector3(1.0f)));
  document.nodes.push_back(
    primitiveNode("c", "b", ScenePrimitiveShape::Ellipse, Vector3(2.0f)));
  SceneInstance instance;
  std::string error;
  testTrue(counters, instance.load(document, "/app", error), "loads");
  const ISceneRenderAttachment* before =
    instance.graph().getAttachment(instance.handleOf("a"), 0);
  const uint64_t revision = instance.revision();
  Transform3D moved;
  moved.position = Vector3(3.0f, 4.0f, 5.0f);
  testTrue(counters, instance.setTransform("a", moved), "transform edit");
  testTrue(counters,
           instance.graph().getAttachment(instance.handleOf("a"), 0) == before,
           "a transform edit keeps the attachment");
  testTrue(counters, instance.revision() > revision, "edits bump the revision");
  testTrue(counters,
           instance.document().nodes[0].transform.position.y == 4.0f,
           "the document reflects the edit");
  Transform3D invalid;
  invalid.scale = Vector3(0.0f, 1.0f, 1.0f);
  const uint64_t beforeInvalid = instance.revision();
  testTrue(counters,
           !instance.setTransform("a", invalid) &&
             instance.revision() == beforeInvalid &&
             instance.findNode("a")->transform.position.x == 3.0f,
           "an invalid transform changes nothing");
  std::vector<SceneComponent> components = instance.findNode("a")->components;
  std::get<ScenePrimitive>(components[0].value).shape =
    ScenePrimitiveShape::Pyramid;
  testTrue(
    counters, instance.setComponents("a", components, error), "component edit");
  testTrue(counters,
           std::get<ScenePrimitive>(instance.findNode("a")->components[0].value)
               .shape == ScenePrimitiveShape::Pyramid,
           "component edit recorded");
  SceneComponent bogus;
  bogus.value = SceneMeshRenderer{ "nothing", ColorRgba{}, true };
  testTrue(counters,
           !instance.setComponents("a", { bogus }, error) && !error.empty(),
           "components naming unknown assets are rejected");
  testTrue(counters,
           instance.setTransforms({ "b", "c" },
                                  { Transform3D::fromPosition(Vector3(7.0f)),
                                    Transform3D::fromPosition(Vector3(8.0f)) }),
           "batched transforms");
  testTrue(counters,
           instance.findNode("c")->transform.position.x == 8.0f,
           "batched transform recorded");
  testTrue(counters,
           instance.setName("b", "Renamed") &&
             instance.findNode("b")->name == "Renamed" &&
             !instance.setName("b", "two\nlines"),
           "rename validates");
  testTrue(counters,
           instance.setVisible("b", false) && !instance.findNode("b")->visible,
           "visibility edit");
  testTrue(counters,
           instance.subtreeIds("a").size() == 3 &&
             instance.subtreeIds("b").size() == 2,
           "subtree ids");
  testTrue(counters,
           instance.removeSubtree("b") && instance.nodeCount() == 1 &&
             instance.findNode("c") == nullptr,
           "removing a subtree removes descendants");
  testTrue(counters,
           instance.document().nodes.size() == 1,
           "the document drops removed nodes");
  const uint64_t beforeEditor = instance.revision();
  SceneEditorState editor;
  editor.cameraX = 4.0;
  instance.setEditorState(editor);
  testTrue(counters,
           instance.revision() == beforeEditor &&
             instance.document().hasEditor &&
             instance.document().editor.cameraX == 4.0,
           "editor state is saved but is not an edit");
  return counters.failures;
}

static int
testInstanceSiblingOrder()
{
  TestCounters counters;
  SceneInstance instance;
  SceneDocument document;
  document.nodes.push_back(
    primitiveNode("p", "", ScenePrimitiveShape::Cube, Vector3(0.0f)));
  document.nodes.push_back(
    primitiveNode("x", "p", ScenePrimitiveShape::Cube, Vector3(0.0f)));
  document.nodes.push_back(
    primitiveNode("z", "p", ScenePrimitiveShape::Cube, Vector3(0.0f)));
  std::string error;
  testTrue(counters, instance.load(document, "/app", error), "loads");
  testTrue(counters,
           instance.insertNode(
             primitiveNode("y", "p", ScenePrimitiveShape::Cube, Vector3(0.0f)),
             "z",
             error),
           "insert before a sibling");
  const std::vector<std::string> children = instance.childIds("p");
  testTrue(counters,
           children.size() == 3 && children[0] == "x" && children[1] == "y" &&
             children[2] == "z",
           "insert position respected");
  testEqStr(counters, instance.nextSiblingId("y"), "z", "next sibling");
  testEqStr(counters, instance.nextSiblingId("z"), "", "last sibling");
  testTrue(counters,
           instance.setParent("z", "p", "x", error),
           "reorder within a parent");
  const SceneDocument& saved = instance.document();
  testTrue(counters,
           saved.nodes.size() == 4 && saved.nodes[1].id == "z" &&
             saved.nodes[2].id == "x" && saved.nodes[3].id == "y",
           "document order follows sibling order");
  testTrue(counters,
           !instance.insertNode(
             primitiveNode("w", "p", ScenePrimitiveShape::Cube, Vector3(0.0f)),
             "p",
             error),
           "insert position must be a sibling");
  testTrue(counters,
           !instance.insertNode(
             primitiveNode("x", "", ScenePrimitiveShape::Cube, Vector3(0.0f)),
             "",
             error),
           "duplicate ids are rejected");
  testTrue(
    counters, !instance.setParent("p", "x", "", error), "cycles are rejected");
  testTrue(counters,
           instance.uniqueId("x") != "x" && instance.uniqueId().size() > 1,
           "unique ids avoid existing nodes");
  return counters.failures;
}

static int
testInstancePicking()
{
  TestCounters counters;
  SceneDocument document;
  document.nodes.push_back(
    primitiveNode("under", "", ScenePrimitiveShape::Rect, Vector3(0.0f)));
  document.nodes.push_back(
    primitiveNode("over", "", ScenePrimitiveShape::Rect, Vector3(0.3f, 0, 0)));
  SceneNode rotated =
    primitiveNode("rotated", "", ScenePrimitiveShape::Rect, Vector3(5, 0, 0));
  std::get<ScenePrimitive>(rotated.components[0].value).extent =
    Vector3(2.0f, 0.2f, 0.5f);
  rotated.transform.rotation =
    glm::angleAxis(glm::radians(90.0f), Vector3(0.0f, 0.0f, 1.0f));
  document.nodes.push_back(rotated);
  SceneNode hidden =
    primitiveNode("hidden", "", ScenePrimitiveShape::Rect, Vector3(0.1f, 0, 0));
  hidden.visible = false;
  document.nodes.push_back(hidden);
  SceneNode empty;
  empty.id = "empty";
  empty.transform.position = Vector3(-5.0f, 0.0f, 0.0f);
  document.nodes.push_back(empty);
  SceneInstanceOptions options;
  options.pickProxies = true;
  SceneInstance instance(nullptr, options);
  std::string error;
  testTrue(counters, instance.load(document, "/app", error), "loads");
  const Vector3 down(0.0f, 0.0f, -1.0f);
  std::string id;
  testTrue(
    counters,
    instance.pickRay(Vector3(0.1f, 0.0f, 10.0f), down, &id) && id == "over",
    "overlapping 2D shapes pick the later one; hidden nodes are skipped");
  testTrue(counters,
           instance.pickRay(Vector3(-0.4f, 0.0f, 10.0f), down, &id) &&
             id == "under",
           "the uncovered part picks the lower shape");
  testTrue(counters,
           instance.pickRay(Vector3(5.0f, 1.5f, 10.0f), down, &id) &&
             id == "rotated",
           "rotation is respected");
  testTrue(counters,
           !instance.pickRay(Vector3(6.5f, 0.0f, 10.0f), down, &id),
           "the unrotated footprint does not hit");
  testTrue(counters,
           instance.pickRay(Vector3(-5.0f, 0.1f, 10.0f), down, &id) &&
             id == "empty",
           "pick proxies make empty nodes selectable");
  testTrue(counters,
           !instance.pickRay(Vector3(0.0f), Vector3(0.0f), &id),
           "a zero ray hits nothing");
  return counters.failures;
}

static int
testInstantiate2000()
{
  TestCounters counters;
  HeadlessRenderFixture fixture(640, 480);
  fixture.mock.Initialize();
  AssetManager assets(&fixture.renderer, false);
  SceneDocument document;
  for (int index = 0; index < 2000; ++index) {
    document.nodes.push_back(primitiveNode(
      "n" + std::to_string(index),
      index % 4 == 0 || index == 0 ? "" : "n" + std::to_string(index - 1),
      ScenePrimitiveShape::Cube,
      Vector3(static_cast<float>(index % 50),
              static_cast<float>(rowOf(index, 50)),
              0)));
  }
  SceneInstance instance(&assets);
  instance.setRenderer(&fixture.renderer);
  std::string error;
  const std::chrono::steady_clock::time_point start =
    std::chrono::steady_clock::now();
  const bool loaded = instance.load(document, "/app", error);
  const double loadMs = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - start)
                          .count();
  testTrue(counters, loaded && instance.nodeCount() == 2000, "2000 nodes load");
  std::vector<std::string> ids;
  std::vector<Transform3D> transforms;
  for (int index = 0; index < 2000; index += 2) {
    ids.push_back("n" + std::to_string(index));
    transforms.push_back(Transform3D::fromPosition(Vector3(1.0f)));
  }
  const std::chrono::steady_clock::time_point editStart =
    std::chrono::steady_clock::now();
  const bool edited = instance.setTransforms(ids, transforms);
  const double editMs = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - editStart)
                          .count();
  testTrue(counters, edited, "1000 batched transform edits");
  const std::chrono::steady_clock::time_point saveStart =
    std::chrono::steady_clock::now();
  const std::string encoded = IlscCodec::encode(instance.document());
  const double saveMs = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - saveStart)
                          .count();
  testTrue(counters, !encoded.empty(), "document encodes");
  std::printf("BENCH: load 2000 nodes %.2f ms, 1000 transform edits %.2f ms, "
              "document+encode %.2f ms (%zu bytes)\n",
              loadMs,
              editMs,
              saveMs,
              encoded.size());
  return counters.failures;
}

void
registerSceneInstanceTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Content.InstancePrimitives",
               []() { return testInstancePrimitives(); });
  registry.add("Illumo.Content.InstanceEnvironmentLight",
               []() { return testInstanceEnvironmentLight(); });
  registry.add("Illumo.Content.InstanceSkyboxAndAssets",
               []() { return testInstanceSkyboxAndAssets(); });
  registry.add("Illumo.Content.InstanceIncrementalEdit",
               []() { return testInstanceIncrementalEdit(); });
  registry.add("Illumo.Content.InstanceSiblingOrder",
               []() { return testInstanceSiblingOrder(); });
  registry.add("Illumo.Content.InstancePicking",
               []() { return testInstancePicking(); });
  registry.add("Illumo.Content.Bench.Instantiate2000",
               []() { return testInstantiate2000(); });
}
