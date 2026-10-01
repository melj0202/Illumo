#include "EditorAssetBrowser.h"
#include "EditorAssets.h"
#include "IllEdPlatform.h"
#include "PanelTestHelpers.h"
#include <Illumo/Content/VirtualFileSystem.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <filesystem>
#include <fstream>

// A PNG signature and IHDR chunk: enough for the dimension probe.
static std::vector<unsigned char>
pngHeader(std::uint32_t width, std::uint32_t height)
{
  std::vector<unsigned char> bytes = { 0x89, 'P',  'N', 'G', 0x0D, 0x0A,
                                       0x1A, 0x0A, 0,   0,   0,    13,
                                       'I',  'H',  'D', 'R' };
  for (std::uint32_t value : { width, height }) {
    bytes.push_back(static_cast<unsigned char>(value >> 24));
    bytes.push_back(static_cast<unsigned char>(value >> 16));
    bytes.push_back(static_cast<unsigned char>(value >> 8));
    bytes.push_back(static_cast<unsigned char>(value));
  }
  // Bit depth, colour type, compression, filter, interlace and a CRC, then
  // an empty IDAT header: stb's header scan stops at the first IDAT.
  const unsigned char tail[] = { 8, 6, 0, 0, 0,   0,   0,   0,  0,
                                 0, 0, 0, 0, 'I', 'D', 'A', 'T' };
  bytes.insert(bytes.end(), tail, tail + sizeof(tail));
  return bytes;
}

static int
testKindsAndIds()
{
  TestCounters counters;
  testTrue(counters,
           EditorAssets::kindFor("/p/meshes/Tree.OBJ") ==
               EditorAssetKind::Mesh &&
             EditorAssets::kindFor("a/b.png") == EditorAssetKind::Texture &&
             EditorAssets::kindFor("x.jpeg") == EditorAssetKind::Texture &&
             EditorAssets::kindFor("s.ilsc") == EditorAssetKind::Scene &&
             EditorAssets::kindFor("notes.txt") == EditorAssetKind::Other &&
             EditorAssets::kindFor("dir.obj/file") == EditorAssetKind::Other,
           "file kinds come from the extension");
  testEqStr(counters,
            EditorAssets::importFolder(EditorAssetKind::Texture),
            "textures",
            "textures import into textures/");
  SceneDocument document;
  testEqStr(counters,
            EditorAssets::assetIdFor("/project/meshes/old tree.obj", document),
            "old_tree",
            "ids come from the file stem");
  SceneAsset taken;
  taken.id = "old_tree";
  taken.type = SceneAssetType::Mesh;
  taken.path = "meshes/other.obj";
  document.assets.push_back(taken);
  testEqStr(counters,
            EditorAssets::assetIdFor("old tree.obj", document),
            "old_tree_2",
            "ids are unique within the document");
  SceneNode node;
  testTrue(counters,
           EditorAssets::nodeFor(taken, &node) && node.components.size() == 1 &&
             std::get<SceneMeshRenderer>(node.components.front().value).asset ==
               "old_tree",
           "a mesh asset becomes a mesh renderer");
  return counters.failures;
}

static int
testImportValidatesTextureCap()
{
  TestCounters counters;
  std::string error;
  testTrue(
    counters,
    EditorAssets::validateImport("small.png", pngHeader(256, 256), &error),
    "a small texture passes");
  testTrue(
    counters,
    EditorAssets::validateImport("edge.png", pngHeader(2048, 2048), &error),
    "a texture exactly at the cap passes");
  testTrue(
    counters,
    !EditorAssets::validateImport("huge.png", pngHeader(4096, 2049), &error) &&
      error.find("16 MiB") != std::string::npos,
    "a texture over one upload is refused with a reason");
  testTrue(counters,
           !EditorAssets::validateImport("broken.png", { 1, 2, 3 }, &error),
           "an unreadable image is refused");
  testTrue(counters,
           EditorAssets::validateImport("mesh.obj", { 'v' }, &error),
           "non-image files are not probed");
  return counters.failures;
}

static int
testBrowserListsTree()
{
  TestCounters counters;
  const std::filesystem::path root =
    std::filesystem::temp_directory_path() / "illed-asset-browser";
  std::error_code code;
  std::filesystem::remove_all(root, code);
  std::filesystem::create_directories(root / "app");
  std::filesystem::create_directories(root / "project" / "meshes");
  std::ofstream(root / "app" / "a.txt") << "a";
  std::ofstream(root / "project" / "meshes" / "tri.obj") << "v 0 0 0";
  std::shared_ptr<VirtualFileSystem> tree =
    std::make_shared<VirtualFileSystem>();
  std::string error;
  VfsMount app;
  app.point = "/app";
  app.layers.push_back(
    { DirectoryVfsBackend::open(root / "app", false, error), "demo" });
  VfsMount project;
  project.point = "/project";
  project.layers.push_back(
    { DirectoryVfsBackend::open(root / "project", true, error), "scene" });
  tree->mount(app, error);
  tree->mount(project, error);
  IllEdNativeTree::install(tree);

  HeadlessRenderFixture fixture(1280, 720);
  EditorAssetBrowser browser(&fixture.window, &fixture.renderer);
  browser.setPlacement(panelArea(0.0f, 400.0f, 240.0f, 280.0f));
  browser.update(nullptr, 0.016f);
  std::vector<GuiFileTreeRow> rows = browser.rowsForTesting();
  testTrue(counters,
           rows.size() == 2 && rows[0].path == "/app" &&
             rows[1].path == "/project" && rows[1].directory,
           "the root lists the mounts");
  browser.clickRowForTesting(1);
  browser.update(nullptr, 0.016f);
  rows = browser.rowsForTesting();
  testTrue(counters,
           rows.size() == 3 && rows[2].path == "/project/meshes",
           "clicking a directory lists and expands it");
  browser.clickRowForTesting(2);
  browser.update(nullptr, 0.016f);
  rows = browser.rowsForTesting();
  testTrue(counters,
           rows.size() == 4 && rows[3].path == "/project/meshes/tri.obj" &&
             rows[3].size == 7,
           "nested files appear with sizes");
  browser.clickRowForTesting(3, true);
  testEqStr(counters,
            browser.takeActivated(),
            "/project/meshes/tri.obj",
            "a double-click activates a file once");
  testTrue(
    counters, browser.takeActivated().empty(), "activation is taken once");
  browser.dragRowOutForTesting(3, 600.0f, 300.0f);
  const EditorAssetBrowser::Drop drop = browser.takeDrop();
  testTrue(counters,
           drop.path == "/project/meshes/tri.obj" && drop.surface == 0u &&
             drop.pixelX == 600.0f && drop.pixelY == 300.0f,
           "a file dragged out is dropped once, with its release point");
  testTrue(counters, browser.takeDrop().path.empty(), "the drop is taken once");
  testTrue(
    counters, browser.getVisual().textCount() > 0u, "the panel draws its rows");
  browser.setPlacement(panelArea(0.0f, 400.0f, 240.0f, 0.0f, false));
  browser.update(nullptr, 0.016f);
  testTrue(counters,
           !browser.containsScreenPoint(10.0f, 450.0f),
           "a hidden browser takes no input");
  IllEdNativeTree::install(nullptr);
  std::filesystem::remove_all(root, code);
  return counters.failures;
}

void
registerEditorAssetsTests(IllumoTestRegistry& registry)
{
  registry.add("IllEd.Assets.KindsAndIds", []() { return testKindsAndIds(); });
  registry.add("IllEd.Assets.ImportValidatesTextureCap",
               []() { return testImportValidatesTextureCap(); });
  registry.add("IllEd.Assets.BrowserListsTree",
               []() { return testBrowserListsTree(); });
}
