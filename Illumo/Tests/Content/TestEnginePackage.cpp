#include <Illumo/Content/EnginePackage.h>
#include <Illumo/Content/PackageArchive.h>
#include <Illumo/Content/PackageMounts.h>
#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/AssetSource.h>
#include <Illumo/Rendering/Font.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/ShaderPreprocessor.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

static void
writeText(const std::filesystem::path& path, const std::string& text)
{
  std::error_code code;
  std::filesystem::create_directories(path.parent_path(), code);
  std::ofstream stream(path, std::ios::binary);
  stream << text;
}

static std::vector<unsigned char>
fileBytes(const std::filesystem::path& path)
{
  std::ifstream stream(path, std::ios::binary);
  return std::vector<unsigned char>(std::istreambuf_iterator<char>(stream),
                                    std::istreambuf_iterator<char>());
}

static std::string
engineManifest(const std::string& id)
{
  return R"({"format":"ilpk","format_version":1,"id":")" + id +
         R"(","kind":"content"})";
}

// Packs the repository's engine files into runtime/engine.ilpk the way the
// distribution does: Assets/ at the root, Shader/ beside it.
static bool
packEngine(const std::filesystem::path& staging,
           const std::filesystem::path& runtime,
           const std::string& id,
           std::string& error)
{
  std::error_code code;
  std::filesystem::create_directories(staging / "Shader", code);
  std::filesystem::create_directories(runtime, code);
  std::filesystem::copy(ILLUMO_ENGINE_ASSETS,
                        staging,
                        std::filesystem::copy_options::recursive,
                        code);
  if (code) {
    error = code.message();
    return false;
  }
  std::filesystem::copy(ILLUMO_ENGINE_SHADERS,
                        staging / "Shader",
                        std::filesystem::copy_options::recursive,
                        code);
  if (code) {
    error = code.message();
    return false;
  }
  writeText(staging / "illumo.json", engineManifest(id));
  PackageArchiveWriter writer;
  return writer.addDirectory(staging, error) &&
         writer.write(runtime / EnginePackage::kFileName, error);
}

static int
testEnginePackageOpen()
{
  TestCounters counters;
  const std::filesystem::path root =
    std::filesystem::temp_directory_path() / "illumo-engine-package-open";
  std::error_code code;
  std::filesystem::remove_all(root, code);
  std::string error;

  testTrue(counters,
           EnginePackage::open(root / "empty", PackageCeilings{}, error) ==
               nullptr &&
             error.empty(),
           "a runtime with neither loose assets nor a package has none");

  const std::filesystem::path shipped = root / "shipped";
  testTrue(
    counters,
    packEngine(root / "staging", shipped, EnginePackage::kPackageId, error),
    "the engine files pack into engine.ilpk");
  testTrue(counters,
           EnginePackage::open(shipped, PackageCeilings{}, error) != nullptr,
           "a distribution opens its engine package");

  // A development build's loose Assets/ wins over a stray archive.
  const std::filesystem::path development = root / "development";
  std::filesystem::create_directories(development / "Assets", code);
  std::filesystem::copy_file(shipped / EnginePackage::kFileName,
                             development / EnginePackage::kFileName,
                             code);
  testTrue(counters,
           EnginePackage::open(development, PackageCeilings{}, error) ==
               nullptr &&
             error.empty(),
           "loose Assets/ wins over engine.ilpk");

  const std::filesystem::path wrong = root / "wrong";
  testTrue(counters,
           packEngine(root / "wrong-staging", wrong, "not-the-engine", error),
           "a package with another id packs");
  testTrue(counters,
           EnginePackage::open(wrong, PackageCeilings{}, error) == nullptr &&
             !error.empty(),
           "an engine.ilpk that is not the engine package is an error");

  std::filesystem::remove_all(root, code);
  return counters.failures;
}

static int
testEnginePackageSource()
{
  TestCounters counters;
  const std::filesystem::path root =
    std::filesystem::temp_directory_path() / "illumo-engine-package-source";
  std::error_code code;
  std::filesystem::remove_all(root, code);
  const std::filesystem::path runtime = root / "runtime";
  std::string error;
  if (!packEngine(
        root / "staging", runtime, EnginePackage::kPackageId, error)) {
    testTrue(counters, false, ("packing failed: " + error).c_str());
    return counters.failures;
  }
  std::shared_ptr<EngineArchiveBackend> package =
    EnginePackage::open(runtime, PackageCeilings{}, error);
  testTrue(counters, package != nullptr, "the engine package opens");
  if (!package) {
    return counters.failures;
  }
  EnginePackageSource source(package, runtime, DefaultAssetSource());

  std::string member;
  testTrue(counters,
           source.memberFor("Shader/canvas_vertex.glsl", &member) &&
             member == "Shader/canvas_vertex.glsl",
           "Shader/ names map to the package's Shader/");
  testTrue(counters,
           source.memberFor("Assets/Fonts/Space_Mono/SpaceMono-Regular.ttf",
                            &member) &&
             member == "Fonts/Space_Mono/SpaceMono-Regular.ttf",
           "Assets/ names map to the package root");
  testTrue(counters,
           source.memberFor(
             (runtime / "Assets" / "Branding" / "illumo-badge.png").string(),
             &member) &&
             member == "Branding/illumo-badge.png",
           "absolute names below the runtime map too");
  testTrue(counters,
           !source.memberFor("Assets/Fonts/missing.ttf", nullptr) &&
             !source.memberFor("apps/game/illumo.json", nullptr) &&
             !source.memberFor("../Shader/canvas_vertex.glsl", nullptr),
           "missing members and other names are not the package's");
  testTrue(counters,
           source.canonical("Shader/canvas_vertex.glsl").rfind("engine:", 0) ==
               0 &&
             source.canonical("apps/game/illumo.json").rfind("engine:", 0) ==
               std::string::npos &&
             source.canonical("engine:Shader/canvas_vertex.glsl") ==
               "engine:Shader/canvas_vertex.glsl" &&
             source.stamp("engine:Shader/canvas_vertex.glsl") ==
               source.stamp("Shader/canvas_vertex.glsl") &&
             source.stamp("Shader/canvas_vertex.glsl") != 0,
           "members stamp by content under either name; others reach the "
           "file system");

  std::vector<unsigned char> bytes;
  testTrue(
    counters,
    source.read(source.canonical("Assets/Branding/illumo-splash.png"), bytes) &&
      bytes == fileBytes(std::filesystem::path(ILLUMO_ENGINE_ASSETS) /
                         "Branding" / "illumo-splash.png"),
    "a texture reads back byte for byte");

  // Installed as the default source, engine code reads from the package.
  SetDefaultAssetSource(&source);
  const PreprocessResult shader =
    ShaderPreprocessor::ProcessFile("Shader/canvas_vertex.glsl");
  testTrue(counters,
           shader.success && shader.source.find("#version") == 0,
           "shader files preprocess from the package");
  Font font;
  testTrue(counters,
           font.loadFile("Assets/Fonts/Space_Mono/SpaceMono-Regular.ttf", 16) &&
             font.isValid(),
           "fonts load from the package");
  Font absolute;
  testTrue(counters,
           absolute.loadFile((runtime / "Assets" / "Fonts" / "Space_Mono" /
                              "SpaceMono-Bold.ttf")
                               .string(),
                             16) &&
             absolute.isValid(),
           "fonts named absolutely below the runtime load from the package");
  SetDefaultAssetSource(nullptr);
  testTrue(counters,
           DefaultAssetSource() != &source &&
             DefaultAssetSource()->hasFileSystem(),
           "clearing the default restores the file system");

  // /engine mounts the package for guests.
  writeText(root / "app" / "illumo.json",
            R"({"format":"ilpk","format_version":1,"id":"demo",)"
            R"("kind":"app","app":{"module":"demo.wasm"}})");
  LoadedPackage application;
  VirtualFileSystem vfs;
  std::vector<std::string> warnings;
  testTrue(
    counters,
    PackageMounts::open(root / "app", PackageCeilings{}, application, error) &&
      PackageMounts::mountAll(vfs,
                              application,
                              {},
                              package,
                              std::filesystem::path(),
                              warnings,
                              error),
    "the package mounts as /engine");
  VfsStat stat;
  testTrue(counters,
           vfs.stat("/engine/Branding/illumo-badge.png", stat, error) &&
             stat.kind == VfsKind::File,
           "guests read engine files at /engine");

  std::filesystem::remove_all(root, code);
  return counters.failures;
}

static void
appendText(const std::filesystem::path& path, const std::string& text)
{
  std::ofstream stream(path, std::ios::binary | std::ios::app);
  stream << text;
}

// Repacks staging over runtime/engine.ilpk while the package is in use.
static bool
repack(const std::filesystem::path& staging,
       const std::filesystem::path& runtime,
       std::string& error)
{
  PackageArchiveWriter writer;
  return writer.addDirectory(staging, error) &&
         writer.write(runtime / EnginePackage::kFileName, error);
}

static int
testEnginePackageReload()
{
  TestCounters counters;
  const std::filesystem::path root =
    std::filesystem::temp_directory_path() / "illumo-engine-package-reload";
  std::error_code code;
  std::filesystem::remove_all(root, code);
  const std::filesystem::path staging = root / "staging";
  const std::filesystem::path runtime = root / "runtime";
  std::string error;
  if (!packEngine(staging, runtime, EnginePackage::kPackageId, error)) {
    testTrue(counters, false, ("packing failed: " + error).c_str());
    return counters.failures;
  }
  std::shared_ptr<EngineArchiveBackend> package =
    EnginePackage::open(runtime, PackageCeilings{}, error);
  testTrue(counters, package != nullptr, "the engine package opens");
  if (!package) {
    return counters.failures;
  }
  EnginePackageSource source(package, runtime, DefaultAssetSource());
  const std::string vertex = source.canonical("Shader/canvas_vertex.glsl");
  const std::string fragment = source.canonical("Shader/canvas_frag.glsl");
  const std::string badge =
    source.canonical("Assets/Branding/illumo-badge.png");
  const std::int64_t vertexStamp = source.stamp(vertex);
  const std::int64_t fragmentStamp = source.stamp(fragment);
  const std::int64_t badgeStamp = source.stamp(badge);
  testTrue(counters,
           vertexStamp != 0 && fragmentStamp != 0 && badgeStamp != 0 &&
             vertexStamp != fragmentStamp,
           "members carry content stamps");
  testTrue(counters,
           !package->refresh() && package->generation() == 1u,
           "an unchanged package is not re-read");

  // A host AssetManager over the package, as a distribution runs it.
  std::unique_ptr<MockBackend> backend = std::make_unique<MockBackend>();
  backend->Initialize();
  Renderer renderer(nullptr, nullptr, nullptr, std::move(backend));
  AssetManager assets(&renderer, false, &source);
  assets.setHotReloadEnabled(true);
  ShaderPaths paths;
  paths.vertexPath = "Shader/canvas_vertex.glsl";
  paths.fragmentPath = "Shader/canvas_frag.glsl";
  const ShaderHandle shader =
    assets.acquireShader(paths, AssetLoadMode::Synchronous);
  const TextureHandle texture = assets.acquireTexture(
    "Assets/Branding/illumo-badge.png", {}, AssetLoadMode::Synchronous);
  testTrue(counters,
           assets.getState(shader).state == AssetState::Ready &&
             assets.getState(texture).state == AssetState::Ready,
           "a shader and a texture load from the package");

  // Rebuild the package with one shader changed. The file is not held open,
  // so it can be replaced while in use.
  const std::string marker = "\n// rebuilt while running\n";
  appendText(staging / "Shader" / "canvas_frag.glsl", marker);
  testTrue(counters,
           repack(staging, runtime, error),
           "a rebuilt package replaces the one in use");
  std::this_thread::sleep_for(std::chrono::milliseconds(550));
  assets.pump();
  assets.completePendingForTests();
  assets.pump();
  testTrue(
    counters,
    package->generation() == 2u && source.stamp(fragment) != fragmentStamp &&
      source.stamp(vertex) == vertexStamp && source.stamp(badge) == badgeStamp,
    "only the changed member's stamp moves");
  testTrue(counters,
           assets.getState(shader).revision >= 2u &&
             assets.getState(texture).revision == 1u,
           "hot reload reloads the changed shader and leaves the texture");
  std::vector<unsigned char> bytes;
  testTrue(counters,
           source.read(fragment, bytes) &&
             std::string(bytes.begin(), bytes.end()).find(marker) !=
               std::string::npos,
           "reads take the rebuilt content");

  // Replacements that are not the engine package are ignored.
  writeText(runtime / EnginePackage::kFileName, "not an archive");
  std::string reason;
  testTrue(counters,
           !package->refresh(&reason) && !reason.empty() &&
             package->generation() == 2u && source.read(fragment, bytes),
           "a corrupt replacement keeps the loaded package");
  reason.clear();
  testTrue(counters,
           !package->refresh(&reason) && reason.empty(),
           "a rejected file is reported once");
  writeText(staging / "illumo.json", engineManifest("not-the-engine"));
  testTrue(counters,
           repack(staging, runtime, error) && !package->refresh(&reason) &&
             reason.find(EnginePackage::kPackageId) != std::string::npos &&
             package->generation() == 2u,
           "a package with another id is not taken");

  // Restored, an explicit reload (F5) takes the rebuilt package even with
  // polling off, as in Release builds.
  writeText(staging / "illumo.json", engineManifest(EnginePackage::kPackageId));
  appendText(staging / "Shader" / "canvas_vertex.glsl", marker);
  assets.setHotReloadEnabled(false);
  const std::uint64_t before = assets.getState(shader).revision;
  testTrue(counters,
           repack(staging, runtime, error) && assets.reloadAll() > 0u,
           "an explicit reload is queued");
  assets.completePendingForTests();
  assets.pump();
  testTrue(counters,
           package->generation() == 3u &&
             assets.getState(shader).revision > before &&
             source.stamp(vertex) != vertexStamp,
           "an explicit reload takes the rebuilt package");

  std::filesystem::remove_all(root, code);
  return counters.failures;
}

void
registerEnginePackageTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Content.EnginePackageReload",
               []() { return testEnginePackageReload(); });
  registry.add("Illumo.Content.EnginePackageOpen",
               []() { return testEnginePackageOpen(); });
  registry.add("Illumo.Content.EnginePackageSource",
               []() { return testEnginePackageSource(); });
}
