#include <Illumo/Content/IlscCodec.h>
#include <Illumo/Content/SceneAssetRefs.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cmath>
#include <cstdio>
#include <vector>

static SceneDocument
sampleDocument()
{
  SceneDocument document;
  document.metadata.title = "Forest";
  document.metadata.author = "Illumo";
  document.metadata.description = "Two lines\nof text";
  document.worldMode = SceneWorldMode::World3D;
  document.environment.skybox = "sky";
  document.environment.ambient = Vector3(0.1f, 0.2f, 0.3f);
  document.environment.sun.direction = Vector3(0.0f, -1.0f, 0.0f);
  document.environment.sun.intensity = 0.45f;

  SceneAsset tree;
  tree.id = "tree";
  tree.type = SceneAssetType::Mesh;
  tree.path = "meshes/tree.obj";
  tree.mesh.centerAndNormalize = true;
  tree.mesh.targetRadius = 2.5f;
  SceneAsset sky;
  sky.id = "sky";
  sky.type = SceneAssetType::CubemapCross;
  sky.path = "/engine/Skybox/skybox-daylight.png";
  SceneAsset atlas;
  atlas.id = "ui";
  atlas.type = SceneAssetType::Atlas;
  atlas.path = "textures/ui.png";
  atlas.texture.filter = SceneTextureFilter::Nearest;
  atlas.columns = 6;
  atlas.rows = 4;
  SceneAsset faces;
  faces.id = "faces";
  faces.type = SceneAssetType::CubemapFaces;
  faces.faces = { "sky/px.png", "sky/nx.png", "sky/py.png",
                  "sky/ny.png", "sky/pz.png", "sky/nz.png" };
  document.assets = { tree, sky, atlas, faces };

  SceneNode root;
  root.id = "n1";
  root.name = "Tree";
  root.tags = { "static", "foliage" };
  root.transform.position = Vector3(1.5f, 0.0f, -2.0f);
  root.transform.rotation =
    glm::normalize(Quaternion(0.9238795f, 0.0f, 0.3826834f, 0.0f));
  root.transform.scale = Vector3(2.0f, 2.0f, 2.0f);
  SceneComponent mesh;
  mesh.value =
    SceneMeshRenderer{ "tree", ColorRgba{ 250, 240, 230, 255 }, false };
  SceneComponent wind;
  wind.value =
    SceneOpaqueComponent{ "vendor.wind", R"({"gusts":[1,2],"strength":0.3})" };
  root.components = { mesh, wind };

  SceneNode child;
  child.id = "n2";
  child.parentId = "n1";
  child.name = "Leaf sprite";
  child.visible = false;
  SceneSprite sprite;
  sprite.texture = "ui";
  sprite.hasCell = true;
  sprite.column = 5;
  sprite.row = 3;
  sprite.facing = SceneSpriteFacing::Billboard;
  sprite.flipX = true;
  SceneComponent spriteComponent;
  spriteComponent.value = sprite;
  child.components = { spriteComponent };

  SceneNode sun;
  sun.id = "sun";
  sun.name = "Sun";
  sun.enabled = false;
  SceneComponent light;
  light.value = SceneLight{ Vector3(1.0f, 0.9f, 0.8f), 1.25f, false };
  SceneComponent camera;
  SceneCamera cameraValue;
  cameraValue.projection = SceneProjection::Orthographic;
  cameraValue.zoom = 48.0f;
  cameraValue.primary = true;
  camera.value = cameraValue;
  SceneComponent primitive;
  primitive.value = ScenePrimitive{ ScenePrimitiveShape::WireSphere,
                                    Vector3(0.25f, 0.25f, 0.25f),
                                    ColorRgba{ 1, 2, 3, 4 } };
  sun.components = { light, camera, primitive };
  document.nodes = { root, child, sun };

  document.extensions = { { "illumogame.world", R"({"csim":"worlds/a.csim"})" },
                          { "alpha.tool", "[1,true,null]" } };
  document.hasEditor = true;
  document.editor.cameraX = 12.125;
  document.editor.cameraY = -3.5;
  document.editor.zoom = 40.0f;
  document.editor.snapEnabled = true;
  return document;
}

static int
testIlscRoundTrip()
{
  TestCounters counters;
  const SceneDocument original = sampleDocument();
  std::string error;
  testTrue(counters,
           validateSceneDocument(original, error),
           "sample document is valid");
  testEqStr(counters, error, "", "validation reports no error");
  const std::string text = IlscCodec::encode(original);
  SceneDocument parsed;
  testTrue(
    counters, IlscCodec::parse(text, parsed, error), "encoded scene parses");
  testEqStr(counters, error, "", "parse reports no error");
  testEqStr(counters,
            IlscCodec::encode(parsed),
            text,
            "re-encoding the parsed scene is byte-identical");
  testEqStr(counters, parsed.metadata.title, "Forest", "metadata title");
  testEqStr(counters,
            parsed.metadata.description,
            "Two lines\nof text",
            "description keeps line breaks");
  testTrue(counters, parsed.worldMode == SceneWorldMode::World3D, "world mode");
  testEqStr(counters, parsed.environment.skybox, "sky", "skybox reference");
  testTrue(counters,
           parsed.environment.sun.intensity == 0.45f,
           "float fields round-trip exactly");
  testEqSize(counters, parsed.assets.size(), 4, "asset count");
  testTrue(counters,
           parsed.assets[0].mesh.centerAndNormalize &&
             parsed.assets[0].mesh.targetRadius == 2.5f,
           "mesh options");
  testTrue(counters,
           parsed.assets[2].type == SceneAssetType::Atlas &&
             parsed.assets[2].columns == 6 && parsed.assets[2].rows == 4 &&
             parsed.assets[2].texture.filter == SceneTextureFilter::Nearest,
           "atlas grid and filter");
  testEqStr(counters,
            parsed.assets[3].faces[5],
            "sky/nz.png",
            "cubemap faces keep order");
  testEqSize(counters, parsed.nodes.size(), 3, "node count");
  const SceneNode& root = parsed.nodes[0];
  testTrue(counters,
           root.tags.size() == 2 && root.tags[1] == "foliage",
           "tags round-trip");
  testTrue(counters,
           std::fabs(root.transform.rotation.y - 0.3826834f) < 1.0e-6f &&
             root.transform.scale.x == 2.0f,
           "transform round-trips");
  const SceneMeshRenderer* mesh =
    std::get_if<SceneMeshRenderer>(&root.components[0].value);
  testTrue(counters,
           mesh != nullptr && mesh->asset == "tree" && !mesh->castShadows &&
             mesh->tint.g == 240,
           "mesh component");
  const SceneOpaqueComponent* wind =
    std::get_if<SceneOpaqueComponent>(&root.components[1].value);
  testTrue(counters,
           wind != nullptr && wind->type == "vendor.wind" &&
             wind->data == R"({"gusts":[1,2],"strength":0.3})",
           "opaque component data is preserved canonically");
  const SceneNode& child = parsed.nodes[1];
  const SceneSprite* sprite =
    std::get_if<SceneSprite>(&child.components[0].value);
  testTrue(counters,
           child.parentId == "n1" && !child.visible && sprite != nullptr &&
             sprite->hasCell && sprite->column == 5 && sprite->row == 3 &&
             sprite->flipX && !sprite->flipY &&
             sprite->facing == SceneSpriteFacing::Billboard,
           "child sprite");
  const SceneNode& sun = parsed.nodes[2];
  const SceneCamera* camera =
    std::get_if<SceneCamera>(&sun.components[1].value);
  const ScenePrimitive* primitive =
    std::get_if<ScenePrimitive>(&sun.components[2].value);
  testTrue(counters,
           !sun.enabled && camera != nullptr &&
             camera->projection == SceneProjection::Orthographic &&
             camera->primary && camera->zoom == 48.0f,
           "camera component");
  testTrue(counters,
           primitive != nullptr &&
             primitive->shape == ScenePrimitiveShape::WireSphere &&
             primitive->color.a == 4,
           "primitive component");
  testTrue(counters,
           parsed.hasEditor && parsed.editor.cameraX == 12.125 &&
             parsed.editor.snapEnabled,
           "editor block round-trips");
  SceneDocument withoutEditor;
  testTrue(counters,
           IlscCodec::parse(
             IlscCodec::encode(original, false), withoutEditor, error) &&
             !withoutEditor.hasEditor,
           "editor block can be omitted");
  testEqStr(counters,
            IlscCodec::withExtension("Scene"),
            "Scene.ilsc",
            "extension appended");
  testEqStr(counters,
            IlscCodec::withExtension("Scene.ILSC"),
            "Scene.ILSC",
            "existing extension kept");
  return counters.failures;
}

static int
testIlscCanonicalGolden()
{
  TestCounters counters;
  SceneDocument document;
  SceneNode node;
  node.id = "n1";
  node.name = "Box";
  node.transform.position = Vector3(0.45f, 1.0f, -0.1f);
  SceneComponent primitive;
  primitive.value = ScenePrimitive{};
  node.components = { primitive };
  document.nodes = { node };
  const std::string expected = R"({
  "format": "ilsc",
  "format_version": [2, 0],
  "metadata": {
    "title": "",
    "author": "",
    "description": ""
  },
  "settings": {
    "world_mode": "2d",
    "environment": {
      "skybox": null,
      "skybox_tint": [1.0, 1.0, 1.0],
      "ambient": [0.25, 0.27, 0.3],
      "sun": {
        "direction": [-0.4, -1.0, -0.3],
        "color": [1.0, 0.96, 0.9],
        "intensity": 1.0,
        "shadows": true
      }
    }
  },
  "assets": [],
  "nodes": [
    {
      "id": "n1",
      "parent": null,
      "name": "Box",
      "enabled": true,
      "visible": true,
      "transform": {
        "position": [0.45, 1.0, -0.1],
        "rotation": [0.0, 0.0, 0.0, 1.0],
        "scale": [1.0, 1.0, 1.0]
      },
      "components": [
        {
          "type": "primitive",
          "shape": "cube",
          "extent": [0.5, 0.5, 0.5],
          "color": [200, 200, 200, 255]
        }
      ]
    }
  ],
  "extensions": {}
}
)";
  const std::string text = IlscCodec::encode(document);
  testEqStr(counters, text, expected, "canonical encoding matches the golden");
  if (text != expected) {
    std::printf("%s\n", text.c_str());
  }
  return counters.failures;
}

static const char*
minimalPrefix()
{
  return R"({"format":"ilsc","format_version":[2,0],)";
}

static int
testIlscRejects()
{
  TestCounters counters;
  struct Case
  {
    std::string text;
    const char* expect;
  };
  const std::string prefix = minimalPrefix();
  const std::vector<Case> cases = {
    { "not json", "not valid JSON" },
    { R"({"format":"ilsc","version":1,"nodes":[]})", "format 1" },
    { R"({"format":"ilsc","format_version":[3,0],"nodes":[]})",
      "not supported" },
    { R"({"format":"ilsc","format_version":[2,7],"nodes":[]})",
      "requires format 2.7" },
    { R"({"format":"zip","format_version":[2,0],"nodes":[]})", "format" },
    { prefix + R"("nodes":[],"surprise":1})", "unknown key" },
    { prefix + R"("nodes":[{"id":"a","colour":1}]})", "unknown key" },
    { prefix + R"("nodes":[{"id":"a"},{"id":"a"}]})", "duplicate node" },
    { prefix + R"("nodes":[{"id":"b","parent":"a"},{"id":"a"}]})",
      "appear earlier" },
    { prefix + R"("nodes":[{"id":"a","parent":"a"}]})", "appear earlier" },
    { prefix + R"("nodes":[{"id":"a","parent":"missing"}]})",
      "appear earlier" },
    { prefix + R"("nodes":[{"id":""}]})", "node id" },
    { prefix + R"("nodes":[{"id":"a","components":[{"type":"hologram"}]}]})",
      "requires a newer format" },
    { prefix +
        R"("nodes":[{"id":"a","components":[{"type":"mesh","asset":"x"}]}]})",
      "mesh asset" },
    { prefix +
        R"("nodes":[{"id":"a","components":[{"type":"primitive","shape":"blob"}]}]})",
      "primitive" },
    { prefix +
        R"("nodes":[{"id":"a","components":[{"type":"primitive","shape":"cube","extent":[0,1,1]}]}]})",
      "extent" },
    { prefix +
        R"("nodes":[{"id":"a","components":[{"type":"primitive","shape":"cube"},{"type":"primitive","shape":"rect"}]}]})",
      "appears twice" },
    { prefix +
        R"("nodes":[{"id":"a","components":[{"type":"light","kind":"point"}]}]})",
      "light" },
    { prefix +
        R"("nodes":[{"id":"a","components":[{"type":"camera","near":5,"far":1}]}]})",
      "camera" },
    { prefix + R"("nodes":[{"id":"a","transform":{"scale":[0,1,1]}}]})",
      "scale" },
    { prefix + R"("nodes":[{"id":"a","transform":{"rotation":[0,0,0,0]}}]})",
      "transform" },
    { prefix + R"("nodes":[{"id":"a","transform":{"position":[1e40,0,0]}}]})",
      "transform" },
    { prefix + R"("nodes":[{"id":"a","name":"two\nlines"}]})", "name" },
    { prefix + R"("nodes":[{"id":"a","tags":["x","x"]}]})", "tags" },
    { prefix +
        R"("assets":[{"id":"t","type":"texture","path":"../x.png"}],"nodes":[]})",
      "path" },
    { prefix +
        R"("assets":[{"id":"t","type":"texture","path":"/"}],"nodes":[]})",
      "path" },
    { prefix +
        R"("assets":[{"id":"t","type":"texture","path":"a.png"},{"id":"t","type":"mesh","path":"b.obj"}],"nodes":[]})",
      "duplicate asset" },
    { prefix +
        R"("assets":[{"id":"t","type":"video","path":"a.mp4"}],"nodes":[]})",
      "unknown asset type" },
    { prefix +
        R"("assets":[{"id":"t","type":"texture","path":"a.png","options":{"grid":[2,2]}}],"nodes":[]})",
      "unknown key" },
    { prefix +
        R"("assets":[{"id":"t","type":"atlas","path":"a.png"}],"nodes":[]})",
      "grid" },
    { prefix +
        R"("assets":[{"id":"t","type":"texture","path":"a.png"}],"nodes":[{"id":"a","components":[{"type":"sprite","texture":"t","cell":[0,0]}]}]})",
      "atlas" },
    { prefix + R"("settings":{"environment":{"skybox":"none"}},"nodes":[]})",
      "cubemap" },
    { prefix + R"("settings":{"world_mode":"4d"},"nodes":[]})", "world_mode" },
    { prefix + R"("extensions":{"nodot":1},"nodes":[]})", "namespaced" },
    { prefix + R"("editor":{"camera":{"zoom":0}},"nodes":[]})", "editor" },
    { prefix + R"("editor":{"layout":{}},"nodes":[]})", "unknown key" }
  };
  for (const Case& entry : cases) {
    SceneDocument document;
    document.metadata.title = "sentinel";
    std::string error;
    const bool accepted = IlscCodec::parse(entry.text, document, error);
    if (accepted || document.metadata.title != "sentinel" ||
        error.find(entry.expect) == std::string::npos) {
      std::printf("FAIL: %s\n  error: %s\n", entry.text.c_str(), error.c_str());
      ++counters.failures;
    }
  }
  testTrue(counters, true, "every malformed scene is rejected with context");
  return counters.failures;
}

static int
testIlscPreservesExtensions()
{
  TestCounters counters;
  const std::string text = std::string(minimalPrefix()) +
                           R"("nodes":[{"id":"a","components":[
      {"type":"studio.physics","mass":2.5,"shape":{"kind":"box"},"tags":["x"]}]}],
    "extensions":{"zeta.last":{"b":1,"a":2},"alpha.first":[3,2,1]}})";
  SceneDocument document;
  std::string error;
  testTrue(counters,
           IlscCodec::parse(text, document, error),
           "foreign namespaced data parses");
  const SceneOpaqueComponent* physics =
    std::get_if<SceneOpaqueComponent>(&document.nodes[0].components[0].value);
  testTrue(counters,
           physics != nullptr && physics->type == "studio.physics" &&
             physics->data ==
               R"({"mass":2.5,"shape":{"kind":"box"},"tags":["x"]})",
           "namespaced component keeps every member");
  testTrue(counters,
           document.nodes[0].findOpaque("studio.physics") != nullptr,
           "opaque lookup by type");
  const std::string encoded = IlscCodec::encode(document);
  SceneDocument again;
  testTrue(counters,
           IlscCodec::parse(encoded, again, error) &&
             IlscCodec::encode(again) == encoded,
           "opaque data survives repeated round trips");
  testTrue(counters,
           encoded.find("\"alpha.first\"") < encoded.find("\"zeta.last\""),
           "extensions are written in sorted key order");
  testTrue(counters,
           again.extensions.size() == 2 &&
             again.extensions[1].key == "zeta.last" &&
             again.extensions[1].data == R"({"a":2,"b":1})",
           "extension values are canonical");
  return counters.failures;
}

static int
testSceneAssetRefsResolve()
{
  TestCounters counters;
  std::string root;
  testTrue(counters,
           scenePackageRoot("/packages/forest/scenes/a.ilsc", root) &&
             root == "/packages/forest",
           "package root under /packages");
  testTrue(counters,
           scenePackageRoot("/project/scenes/a.ilsc", root) &&
             root == "/project",
           "package root of the project");
  testTrue(counters,
           scenePackageRoot("/app/a.ilsc", root) && root == "/app",
           "package root of the application");
  testTrue(counters,
           !scenePackageRoot("/packages", root) &&
             !scenePackageRoot("/", root) &&
             !scenePackageRoot("relative/a.ilsc", root),
           "no package root for the tree root or relative paths");

  std::string resolved;
  testTrue(
    counters,
    resolveSceneReference("/packages/forest", "meshes/tree.obj", resolved) &&
      resolved == "/packages/forest/meshes/tree.obj",
    "relative reference resolves inside the package");
  testTrue(counters,
           resolveSceneReference("/app", "/engine/Skybox/sky.png", resolved) &&
             resolved == "/engine/Skybox/sky.png",
           "absolute reference is kept");
  testTrue(counters,
           !resolveSceneReference("/app", "../escape.png", resolved) &&
             !resolveSceneReference("/app", "", resolved),
           "escaping and empty references fail");
  testEqStr(counters,
            sceneReferenceFor("/project", "/project/meshes/a.obj"),
            "meshes/a.obj",
            "same-package targets become relative");
  testEqStr(counters,
            sceneReferenceFor("/project", "/engine/Skybox/sky.png"),
            "/engine/Skybox/sky.png",
            "other-package targets stay absolute");

  SceneDocument document = sampleDocument();
  SceneAsset duplicate;
  duplicate.id = "again";
  duplicate.type = SceneAssetType::Texture;
  duplicate.path = "/project/textures/ui.png";
  document.assets.push_back(duplicate);
  const SceneFetchList fetches = collectSceneFetches(document, "/project");
  testEqSize(counters, fetches.paths.size(), 9, "distinct fetch paths");
  testEqStr(counters,
            fetches.paths[0],
            "/project/meshes/tree.obj",
            "first fetch follows asset order");
  testEqStr(counters,
            fetches.paths[3],
            "/project/sky/px.png",
            "cubemap faces fetched in face order");
  testTrue(counters, fetches.unresolved.empty(), "nothing unresolved");

  const std::vector<std::string> libraries = objMaterialLibraries(
    "/project/meshes/tree.obj",
    "# comment\nmtllib tree.mtl\n  mtllib  materials\\bark.mtl\r\nmtllib "
    "tree.mtl\nmtllib ../escape.mtl\nv 0 0 0\n");
  testTrue(counters,
           libraries.size() == 2 &&
             libraries[0] == "/project/meshes/tree.mtl" &&
             libraries[1] == "/project/meshes/materials/bark.mtl",
           "OBJ material libraries resolve beside the mesh");
  return counters.failures;
}

void
registerIlscCodecTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Content.IlscRoundTrip",
               []() { return testIlscRoundTrip(); });
  registry.add("Illumo.Content.IlscCanonicalGolden",
               []() { return testIlscCanonicalGolden(); });
  registry.add("Illumo.Content.IlscRejects",
               []() { return testIlscRejects(); });
  registry.add("Illumo.Content.IlscPreservesExtensions",
               []() { return testIlscPreservesExtensions(); });
  registry.add("Illumo.Content.SceneAssetRefsResolve",
               []() { return testSceneAssetRefsResolve(); });
}
