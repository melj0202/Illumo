#include "IlscCodec.h"
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cmath>
#include <filesystem>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>

static TestCounters g;

static void
testRoundTrip()
{
  testSection("IlscCodec: round trip");
  IlscDocument document;
  document.camera.x = 4.5;
  document.camera.y = -2.0;
  document.camera.zoom = 32.0f;
  IlscNode root;
  root.id = "n1";
  root.name = "Root";
  root.kind = SceneNodeKind::Empty;
  IlscNode cube;
  cube.id = "n2";
  cube.parentId = "n1";
  cube.name = "Box";
  cube.kind = SceneNodeKind::SolidCube;
  cube.transform.position = Vector3(1.0f, 2.0f, 0.0f);
  cube.primitive.extent = Vector3(0.5f, 0.25f, 0.5f);
  cube.primitive.color = ColorRgba{ 10, 20, 30, 255 };
  document.nodes.push_back(root);
  document.nodes.push_back(cube);

  const std::string text = IlscCodec::encode(document);
  IlscDocument loaded;
  std::string error;
  testTrue(g, IlscCodec::parse(text, &loaded, &error), "parses encoded scene");
  testEqInt(g, loaded.version, 1, "version 1");
  testEqSize(g, loaded.nodes.size(), 2u, "two nodes");
  testEqStr(g, loaded.nodes[1].parentId, "n1", "parent preserved");
  testTrue(
    g, loaded.nodes[1].kind == SceneNodeKind::SolidCube, "cube kind preserved");
  testTrue(
    g, loaded.nodes[1].transform.position.x == 1.0f, "position x preserved");
  testTrue(g, loaded.nodes[1].primitive.color.r == 10, "cube color preserved");
}

static void
testMixed2d3dRoundTrip()
{
  testSection("IlscCodec: 2D ellipse and 3D pyramid round trip");
  IlscDocument document;
  document.worldMode = IlscWorldMode::World3D;
  IlscNode ellipse;
  ellipse.id = "n1";
  ellipse.name = "Disk";
  ellipse.kind = SceneNodeKind::FilledEllipse;
  ellipse.transform.position = Vector3(2.0f, -1.0f, 0.0f);
  ellipse.primitive.extent = Vector3(0.75f, 0.4f, 0.5f);
  ellipse.primitive.color = ColorRgba{ 9, 8, 7, 255 };
  IlscNode pyramid;
  pyramid.id = "n2";
  pyramid.parentId = "n1";
  pyramid.name = "Peak";
  pyramid.kind = SceneNodeKind::SolidPyramid;
  pyramid.transform.position = Vector3(0.0f, 3.0f, 1.0f);
  pyramid.primitive.extent = Vector3(1.25f, 2.0f, 0.5f);
  pyramid.primitive.color = ColorRgba{ 1, 2, 3, 255 };
  document.nodes.push_back(ellipse);
  document.nodes.push_back(pyramid);

  const std::string text = IlscCodec::encode(document);
  IlscDocument loaded;
  std::string error;
  testTrue(g, IlscCodec::parse(text, &loaded, &error), "parses mixed scene");
  testTrue(
    g, loaded.worldMode == IlscWorldMode::World3D, "world mode 3d survives");
  testEqStr(g, loaded.nodes[0].id, "n1", "ellipse id");
  testEqStr(g, loaded.nodes[1].parentId, "n1", "pyramid parent");
  testTrue(
    g, loaded.nodes[0].kind == SceneNodeKind::FilledEllipse, "ellipse kind");
  testTrue(
    g, loaded.nodes[1].kind == SceneNodeKind::SolidPyramid, "pyramid kind");
  testTrue(g, loaded.nodes[0].transform.position.x == 2.0f, "ellipse position");
  testTrue(g, loaded.nodes[0].primitive.extent.x == 0.75f, "ellipse extent");
  testTrue(g, loaded.nodes[1].primitive.color.b == 3, "pyramid color");
}

static void
testRejectsCycleAndUnknownKind()
{
  testSection("IlscCodec: rejects invalid graphs");
  IlscDocument document;
  std::string error;
  const char* cycle = R"({
    "format": "ilsc",
    "version": 1,
    "nodes": [
      {"id": "a", "parent": "b", "kind": "empty"},
      {"id": "b", "parent": "a", "kind": "empty"}
    ]
  })";
  testTrue(g, !IlscCodec::parse(cycle, &document, &error), "cycle is rejected");
  const char* unknown = R"({
    "format": "ilsc",
    "version": 1,
    "nodes": [{"id": "a", "kind": "mesh"}]
  })";
  testTrue(g,
           !IlscCodec::parse(unknown, &document, &error),
           "unknown kind is rejected");
  const char* badFormat = R"({"format": "illumo", "version": 1, "nodes": []})";
  testTrue(g,
           !IlscCodec::parse(badFormat, &document, &error),
           "non-ilsc format is rejected");
}

static void
testFileRoundTrip()
{
  testSection("IlscCodec: file round trip");
  IlscDocument document;
  IlscNode node;
  node.id = "n1";
  node.name = "Solo";
  node.kind = SceneNodeKind::SolidCube;
  document.nodes.push_back(node);
  const std::filesystem::path path =
    std::filesystem::temp_directory_path() / u8"codec-\u4e16\u754c-\u00e9.ilsc";
  const std::u8string encodedPath = path.u8string();
  const std::string utf8Path(encodedPath.begin(), encodedPath.end());
  struct TempFileGuard
  {
    std::filesystem::path file;
    ~TempFileGuard()
    {
      std::error_code ec;
      std::filesystem::remove(file, ec);
    }
  } guard{ path };
  std::error_code fsError;
  std::filesystem::remove(path, fsError);
  std::string error;
  testTrue(
    g, IlscCodec::writeFile(utf8Path, document, &error), "writes scene file");
  IlscDocument loaded;
  testTrue(
    g, IlscCodec::readFile(utf8Path, &loaded, &error), "reads scene file");
  testEqSize(g, loaded.nodes.size(), 1u, "loaded one node");
  testEqStr(g, loaded.nodes[0].name, "Solo", "name round trips");
  document.nodes[0].name = "Replacement";
  testTrue(g,
           IlscCodec::writeFile(utf8Path, document, &error),
           "replaces existing scene after finalization");
  testTrue(g,
           IlscCodec::readFile(utf8Path, &loaded, &error) &&
             loaded.nodes[0].name == "Replacement",
           "replacement stays format compatible");
#ifdef _WIN32
  const std::string invalidPath(1, static_cast<char>(0xff));
  testTrue(g,
           !IlscCodec::writeFile(invalidPath, document, &error),
           "invalid UTF-8 save path is contained");
  testTrue(g,
           !IlscCodec::readFile(invalidPath, &loaded, &error),
           "invalid UTF-8 load path is contained");
#endif
}

void
registerIlscCodecTests(IllumoTestRegistry& registry)
{
  registry.add("IllEd.Ilsc.NumericRanges", []() {
    g = {};
    IlscDocument baseline;
    IlscNode node;
    node.id = "sentinel";
    node.kind = SceneNodeKind::SolidCube;
    baseline.nodes.push_back(node);
    const std::string original = IlscCodec::encode(baseline);
    const nlohmann::json base = nlohmann::json::parse(original);
    for (const char* path : { "/camera/yaw",
                              "/camera/pitch",
                              "/camera/zoom",
                              "/nodes/0/transform/position/0",
                              "/nodes/0/transform/scale/1",
                              "/nodes/0/transform/rotation/w",
                              "/nodes/0/primitive/extent/2" }) {
      for (double extreme : { 1e100, -1e100 }) {
        nlohmann::json invalid = base;
        invalid[nlohmann::json::json_pointer(path)] = extreme;
        IlscDocument output = baseline;
        std::string error;
        testTrue(g,
                 !IlscCodec::parse(invalid.dump(), &output, &error),
                 "out-of-float-range value rejected");
        testEqStr(g,
                  IlscCodec::encode(output),
                  original,
                  "numeric rejection preserves destination");
      }
    }
    for (const char* largeInteger :
         { "4294967297", "-4294967295", "18446744073709551615" }) {
      for (const char* path : { "/version", "/nodes/0/primitive/color/0" }) {
        nlohmann::json invalid = base;
        invalid[nlohmann::json::json_pointer(path)] =
          nlohmann::json::parse(largeInteger);
        IlscDocument output = baseline;
        std::string error;
        testTrue(g,
                 !IlscCodec::parse(invalid.dump(), &output, &error),
                 "integer overflow cannot wrap into valid version or color");
        testEqStr(g,
                  IlscCodec::encode(output),
                  original,
                  "integer rejection is transactional");
      }
    }
    baseline.camera.yaw = std::numeric_limits<float>::max();
    baseline.camera.pitch = -std::numeric_limits<float>::max();
    baseline.nodes[0].transform.position.x = std::numeric_limits<float>::max();
    baseline.nodes[0].primitive.extent.x =
      std::numeric_limits<float>::denorm_min();
    const std::string boundary = IlscCodec::encode(baseline);
    IlscDocument output;
    std::string error;
    testTrue(g,
             IlscCodec::parse(boundary, &output, &error),
             "finite float boundaries accepted");
    testTrue(
      g, std::isfinite(output.camera.yaw), "accepted yaw remains finite");
    testEqStr(
      g, IlscCodec::encode(output), boundary, "float boundaries round trip");
    return g.failures;
  });
  registry.add("IllEd.Ilsc.RoundTrip", []() {
    g = {};
    testRoundTrip();
    return g.failures;
  });
  registry.add("IllEd.Ilsc.RejectsInvalid", []() {
    g = {};
    testRejectsCycleAndUnknownKind();
    return g.failures;
  });
  registry.add("IllEd.Ilsc.FileRoundTrip", []() {
    g = {};
    testFileRoundTrip();
    return g.failures;
  });
  registry.add("IllEd.Ilsc.Mixed2d3dRoundTrip", []() {
    g = {};
    testMixed2d3dRoundTrip();
    return g.failures;
  });
}
