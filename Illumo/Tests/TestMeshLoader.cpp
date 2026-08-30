#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/MeshData.h>
#include <Illumo/Rendering/MeshLoader.h>
#include <Illumo/Rendering/Primitives/MeshVisual.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cmath>
#include <memory>
#include <string>

static TestCounters g;

static void
testMeshLoaderBasicMemory()
{
  testSection("MeshLoader: load basic OBJ from memory");

  const std::string objContent = "# Simple triangle\n"
                                 "v 0.0 0.0 0.0\n"
                                 "v 1.0 0.0 0.0\n"
                                 "v 0.0 1.0 0.0\n"
                                 "vn 0.0 0.0 1.0\n"
                                 "vt 0.0 0.0\n"
                                 "vt 1.0 0.0\n"
                                 "vt 0.0 1.0\n"
                                 "f 1/1/1 2/2/1 3/3/1\n";

  MeshLoadOptions options;
  options.flipTexCoordsV = false;
  MeshLoadResult result = MeshLoader::loadFromMemory(objContent, options);

  testTrue(g, result.success, "Basic triangle loaded successfully");
  testEqSize(g, result.mesh.vertices.size(), 3u, "Vertex count is 3");
  testEqSize(g, result.mesh.indices.size(), 3u, "Index count is 3");
  testEqSize(g, result.mesh.submeshes.size(), 1u, "Submesh count is 1");

  const MeshVertex& v0 = result.mesh.vertices[0];
  testTrue(g, std::abs(v0.position.x - 0.0f) < 0.001f, "v0 pos.x matches");
  testTrue(g, std::abs(v0.position.y - 0.0f) < 0.001f, "v0 pos.y matches");
  testTrue(g, std::abs(v0.position.z - 0.0f) < 0.001f, "v0 pos.z matches");
  testTrue(g, std::abs(v0.normal.z - 1.0f) < 0.001f, "v0 normal.z matches");
}

static void
testMeshLoaderQuadTriangulation()
{
  testSection("MeshLoader: quad triangulation and options");

  const std::string quadObj = "v -1.0 -1.0 0.0\n"
                              "v  1.0 -1.0 0.0\n"
                              "v  1.0  1.0 0.0\n"
                              "v -1.0  1.0 0.0\n"
                              "vt 0.0 0.0\n"
                              "vt 1.0 0.0\n"
                              "vt 1.0 1.0\n"
                              "vt 0.0 1.0\n"
                              "f 1/1 2/2 3/3 4/4\n";

  MeshLoadOptions options;
  options.triangulate = true;
  options.flipTexCoordsV = true;
  MeshLoadResult result = MeshLoader::loadFromMemory(quadObj, options);

  testTrue(g, result.success, "Quad OBJ loaded successfully");
  testEqSize(g, result.mesh.vertices.size(), 4u, "4 unique vertices in quad");
  testEqSize(
    g, result.mesh.indices.size(), 6u, "6 indices after triangulation");

  // Verify flipped tex coords
  const MeshVertex& v3 = result.mesh.vertices[3];
  testTrue(g,
           std::abs(v3.texCoords.y - 0.0f) < 0.001f,
           "Flipped texcoord V is 1.0 - 1.0 = 0.0");
}

static void
testMeshDataUtilities()
{
  testSection(
    "MeshData: bounds, normals generation, centering, and flattening");

  MeshData mesh;
  MeshVertex a;
  a.position = glm::vec3(-2.0f, 0.0f, 0.0f);
  MeshVertex b;
  b.position = glm::vec3(2.0f, 0.0f, 0.0f);
  MeshVertex c;
  c.position = glm::vec3(0.0f, 2.0f, 0.0f);

  mesh.vertices = { a, b, c };
  mesh.indices = { 0, 1, 2 };

  mesh.computeBounds();
  testTrue(
    g, std::abs(mesh.minBounds.x - (-2.0f)) < 0.001f, "minBounds.x is -2.0");
  testTrue(g, std::abs(mesh.maxBounds.x - 2.0f) < 0.001f, "maxBounds.x is 2.0");
  testTrue(g, std::abs(mesh.maxBounds.y - 2.0f) < 0.001f, "maxBounds.y is 2.0");

  mesh.computeNormalsIfMissing();
  testTrue(g,
           mesh.vertices[0].normal.z > 0.9f,
           "Generated normal points in +Z direction");

  mesh.centerAndScale(1.0f);
  testTrue(g, mesh.maxBounds.x <= 1.001f, "Scaled mesh fits in target radius");

  std::vector<float> flattened = mesh.toPos3Color3Uv2();
  testEqSize(g,
             flattened.size(),
             3u * 8u,
             "Flattened buffer has 24 floats (3 vertices * 8)");
}

static void
testMeshLoaderErrorHandling()
{
  testSection("MeshLoader: error handling on empty or invalid input");

  MeshLoadResult emptyResult = MeshLoader::loadFromMemory("");
  testTrue(g, !emptyResult.success, "Empty memory content fails safely");

  MeshLoadResult invalidResult =
    MeshLoader::loadFromFile("non_existent_file_xyz_12345.obj");
  testTrue(g, !invalidResult.success, "Non-existent file path fails safely");
  testTrue(g, !invalidResult.error.empty(), "Error message is populated");
}

class MockMeshLoaderBackend : public IMeshLoaderBackend
{
public:
  int loadFromFileCalls = 0;
  int loadFromMemoryCalls = 0;

  bool loadFromFile(const std::string&,
                    const MeshLoadOptions&,
                    MeshLoadResult* outResult) override
  {
    loadFromFileCalls += 1;
    if (outResult != nullptr) {
      outResult->success = true;
      outResult->warning = "MockBackendFromFile";
    }
    return true;
  }

  bool loadFromMemory(const std::string&,
                      const MeshLoadOptions&,
                      const std::string&,
                      MeshLoadResult* outResult) override
  {
    loadFromMemoryCalls += 1;
    if (outResult != nullptr) {
      outResult->success = true;
      outResult->warning = "MockBackendFromMemory";
    }
    return true;
  }
};

static void
testMeshLoaderBackendSwapping()
{
  testSection("MeshLoader: custom backend injection and reset");

  std::shared_ptr<MockMeshLoaderBackend> mockLoader =
    std::make_shared<MockMeshLoaderBackend>();
  MeshLoader::setCustomBackend(mockLoader);

  MeshLoadResult result1 = MeshLoader::loadFromFile("test.custom");
  testTrue(g, result1.success, "Custom backend handled loadFromFile");
  testEqInt(g,
            mockLoader->loadFromFileCalls,
            1,
            "Mock backend loadFromFile called once");
  testEqStr(
    g, result1.warning.c_str(), "MockBackendFromFile", "Mock warning matches");

  MeshLoadResult result2 = MeshLoader::loadFromMemory("data");
  testTrue(g, result2.success, "Custom backend handled loadFromMemory");
  testEqInt(g,
            mockLoader->loadFromMemoryCalls,
            1,
            "Mock backend loadFromMemory called once");
  testEqStr(g,
            result2.warning.c_str(),
            "MockBackendFromMemory",
            "Mock warning matches");

  MeshLoader::resetBackend();

  const std::string objTriangle = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
  MeshLoadResult defaultResult = MeshLoader::loadFromMemory(objTriangle);
  testTrue(g,
           defaultResult.success,
           "Default tinyobjloader restored after resetBackend");
  testEqSize(
    g, defaultResult.mesh.vertices.size(), 3u, "Parsed with default loader");
}

static void
testMeshVisualAddMesh()
{
  testSection("MeshVisual: addMesh integration");

  NullRenderWindow window(640, 480);
  EnvVars env;
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend backend;
  backend.Initialize();
  Renderer renderer(&window, &env, &camera, &backend, false);

  const std::string objContent = "v 0.0 0.0 0.0\n"
                                 "v 10.0 0.0 0.0\n"
                                 "v 0.0 10.0 0.0\n"
                                 "f 1 2 3\n";

  MeshLoadResult loadResult = MeshLoader::loadFromMemory(objContent);
  testTrue(g, loadResult.success, "Loaded mesh for visual");

  MeshVisual visual;
  visual.prepare(&renderer);
  visual.addMesh(loadResult.mesh, ColorRgba{ 255, 128, 64, 255 });

  bool appendOk = visual.AppendCommands(&renderer);
  testTrue(g, appendOk, "MeshVisual::AppendCommands returned true");
}

static int
runMeshLoaderCase(void (*fn)())
{
  g.failures = 0;
  fn();
  return g.failures;
}

void
registerMeshLoaderTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.MeshLoader.BasicMemory",
               []() { return runMeshLoaderCase(testMeshLoaderBasicMemory); });
  registry.add("Illumo.MeshLoader.QuadTriangulation", []() {
    return runMeshLoaderCase(testMeshLoaderQuadTriangulation);
  });
  registry.add("Illumo.MeshLoader.MeshDataUtilities",
               []() { return runMeshLoaderCase(testMeshDataUtilities); });
  registry.add("Illumo.MeshLoader.ErrorHandling",
               []() { return runMeshLoaderCase(testMeshLoaderErrorHandling); });
  registry.add("Illumo.MeshLoader.BackendSwapping", []() {
    return runMeshLoaderCase(testMeshLoaderBackendSwapping);
  });
  registry.add("Illumo.MeshLoader.MeshVisualIntegration",
               []() { return runMeshLoaderCase(testMeshVisualAddMesh); });
}
