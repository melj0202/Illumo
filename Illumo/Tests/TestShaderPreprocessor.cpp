#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/ShaderPreprocessor.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestRegistry.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

static int g_sp_failures = 0;

static void
spTrue(bool cond, const char* msg)
{
  if (!cond) {
    std::printf("FAIL: %s\n", msg);
    ++g_sp_failures;
  } else {
    std::printf("PASS: %s\n", msg);
  }
}

static void
spEqStr(const std::string& a, const std::string& b, const char* msg)
{
  if (a != b) {
    std::printf(
      "FAIL: %s (got '%s', expected '%s')\n", msg, a.c_str(), b.c_str());
    ++g_sp_failures;
  } else {
    std::printf("PASS: %s\n", msg);
  }
}

static void
spContains(const std::string& haystack,
           const std::string& needle,
           const char* msg)
{
  if (haystack.find(needle) == std::string::npos) {
    std::printf("FAIL: %s (did not find '%s')\n", msg, needle.c_str());
    ++g_sp_failures;
  } else {
    std::printf("PASS: %s\n", msg);
  }
}

static void
testVirtualIncludeResolution()
{
  // 1. Default modules
  spTrue(ShaderPreprocessor::HasVirtualInclude("illumo/common.glsl"),
         "HasVirtualInclude illumo/common.glsl");
  spTrue(ShaderPreprocessor::HasVirtualInclude("illumo/screen_transform.glsl"),
         "HasVirtualInclude illumo/screen_transform.glsl");
  spTrue(ShaderPreprocessor::HasVirtualInclude("illumo/vertex_2d.glsl"),
         "HasVirtualInclude illumo/vertex_2d.glsl");
  spTrue(ShaderPreprocessor::HasVirtualInclude("illumo/sprite.glsl"),
         "HasVirtualInclude illumo/sprite.glsl");

  // 2. Preprocessing source with virtual include
  const std::string src = R"(#version 330 core
#include <illumo/screen_transform.glsl>
void main() {
    vec4 clip = illumoScreenToClip(vec2(100.0, 50.0), vec2(800.0, 600.0), 0.0);
    gl_Position = clip;
}
)";

  PreprocessResult result = ShaderPreprocessor::Process(src);
  spTrue(result.success, "Process virtual include success");
  spContains(result.source,
             "illumoScreenToClip",
             "Preprocessed source contains illumoScreenToClip function");
  spContains(result.source,
             "#version 330 core",
             "Preprocessed source contains #version");

  // 3. Custom virtual include registration & unregistration
  ShaderPreprocessor::RegisterVirtualInclude(
    "custom/math.glsl",
    "float customAdd(float a, float b) { return a + b; }\n");
  spTrue(ShaderPreprocessor::HasVirtualInclude("custom/math.glsl"),
         "Custom virtual include registered");

  const std::string customSrc = R"(#include <custom/math.glsl>
void test() { float x = customAdd(1.0, 2.0); }
)";
  PreprocessResult customResult = ShaderPreprocessor::Process(customSrc);
  spTrue(customResult.success, "Process custom virtual include success");
  spContains(customResult.source, "customAdd", "Contains customAdd");

  ShaderPreprocessor::UnregisterVirtualInclude("custom/math.glsl");
  spTrue(!ShaderPreprocessor::HasVirtualInclude("custom/math.glsl"),
         "Custom virtual include unregistered");
}

static void
testDiskIncludeResolution()
{
  std::error_code ec;
  std::filesystem::path tempDir =
    std::filesystem::temp_directory_path(ec) / "illumo_shader_test_disk";
  std::filesystem::create_directories(tempDir, ec);

  std::filesystem::path headerFile = tempDir / "lighting.glsl";
  {
    std::ofstream out(headerFile);
    out << "// Lighting Header\nvec3 calculateLight() { return vec3(1.0); }\n";
  }

  std::filesystem::path mainFile = tempDir / "main.vert";
  {
    std::ofstream out(mainFile);
    out << "#version 330 core\n#include \"lighting.glsl\"\nvoid main() { vec3 "
           "l = calculateLight(); }\n";
  }

  PreprocessResult result = ShaderPreprocessor::ProcessFile(mainFile.string());
  spTrue(result.success, "ProcessFile disk include success");
  spContains(result.source,
             "calculateLight",
             "Preprocessed source contains calculateLight");
  spTrue(!result.fileDependencies.empty(), "Dependencies list non-empty");

  // Clean up test files
  std::filesystem::remove_all(tempDir, ec);
}

static void
testPragmaOnceAndCircularDetection()
{
  // 1. Pragma once test
  ShaderPreprocessor::RegisterVirtualInclude(
    "modules/once.glsl", "#pragma once\nint onceVal = 42;\n");
  const std::string multiIncludeSrc = R"(
#include <modules/once.glsl>
#include <modules/once.glsl>
void main() {}
)";
  PreprocessResult onceResult = ShaderPreprocessor::Process(multiIncludeSrc);
  spTrue(onceResult.success, "Process multi-include with pragma once");

  // Count occurrences of "int onceVal = 42;"
  size_t first = onceResult.source.find("int onceVal = 42;");
  spTrue(first != std::string::npos, "Found onceVal first time");
  size_t second = onceResult.source.find("int onceVal = 42;", first + 1);
  spTrue(second == std::string::npos,
         "onceVal not duplicated thanks to pragma once");

  // 2. Circular include test
  ShaderPreprocessor::RegisterVirtualInclude("circ/a.glsl",
                                             "#include <circ/b.glsl>\n");
  ShaderPreprocessor::RegisterVirtualInclude("circ/b.glsl",
                                             "#include <circ/a.glsl>\n");

  PreprocessResult circResult =
    ShaderPreprocessor::Process("#include <circ/a.glsl>\n");
  spTrue(!circResult.success,
         "Circular include detected and failed gracefully");
  spContains(circResult.errorMessage,
             "Circular include",
             "Error message identifies circular include");

  ShaderPreprocessor::UnregisterVirtualInclude("modules/once.glsl");
  ShaderPreprocessor::UnregisterVirtualInclude("circ/a.glsl");
  ShaderPreprocessor::UnregisterVirtualInclude("circ/b.glsl");
}

static void
testVersionPreservationAndDefines()
{
  const std::string src = R"(
// Some leading comment
#version 330 core
void main() {}
)";

  PreprocessOptions options;
  options.defines = { "ENABLE_LIGHTING 1", "MAX_CASCADES=4", "DEBUG_MODE" };

  PreprocessResult result = ShaderPreprocessor::Process(src, options);
  spTrue(result.success, "Process with defines success");
  spContains(result.source, "#version 330 core", "Contains #version");
  spContains(result.source,
             "#define ENABLE_LIGHTING 1",
             "Contains ENABLE_LIGHTING define");
  spContains(result.source,
             "#define MAX_CASCADES 4",
             "Contains MAX_CASCADES define (normalized from =)");
  spContains(result.source, "#define DEBUG_MODE", "Contains DEBUG_MODE define");

  // Verify #version is on line 1
  size_t verPos = result.source.find("#version 330 core");
  spTrue(verPos == 0,
         "#version is at the very beginning of the preprocessed source");
}

static void
testLineDirectives()
{
  ShaderPreprocessor::RegisterVirtualInclude("mod/header.glsl",
                                             "void helper() {}\n");

  const std::string src = R"(#version 330 core
#include <mod/header.glsl>
void main() { helper(); }
)";

  // With line directives
  PreprocessOptions withLine;
  withLine.emitLineDirectives = true;
  PreprocessResult resWith = ShaderPreprocessor::Process(src, withLine);
  spTrue(resWith.success, "Process with line directives");
  spContains(resWith.source, "#line 1", "Contains #line 1 marker");

  // Without line directives
  PreprocessOptions noLine;
  noLine.emitLineDirectives = false;
  PreprocessResult resNo = ShaderPreprocessor::Process(src, noLine);
  spTrue(resNo.success, "Process without line directives");
  spTrue(resNo.source.find("#line") == std::string::npos,
         "No #line directives in output");

  ShaderPreprocessor::UnregisterVirtualInclude("mod/header.glsl");
}

static void
testAssetManagerHotReloadWithInclude()
{
  std::error_code ec;
  std::filesystem::path tempDir = std::filesystem::temp_directory_path(ec) /
                                  "illumo_am_shader_hotreload_test";
  std::filesystem::create_directories(tempDir, ec);

  std::filesystem::path incPath = tempDir / "common_var.glsl";
  {
    std::ofstream out(incPath);
    out << "// v1\n#define TEST_CONSTANT 100\n";
  }

  std::filesystem::path vsPath = tempDir / "test.vert";
  {
    std::ofstream out(vsPath);
    out
      << "#version 330 core\n#include \"common_var.glsl\"\nlayout (location = "
         "0) in vec3 aPos;\nvoid main() { gl_Position = vec4(aPos, 1.0); }\n";
  }

  std::filesystem::path fsPath = tempDir / "test.frag";
  {
    std::ofstream out(fsPath);
    out << "#version 330 core\n#include \"common_var.glsl\"\nout vec4 "
           "FragColor;\nvoid main() { FragColor = vec4(float(TEST_CONSTANT) / "
           "255.0); }\n";
  }

  std::unique_ptr<MockBackend> backend = std::make_unique<MockBackend>();
  backend->Initialize();
  std::unique_ptr<Renderer> renderer =
    std::make_unique<Renderer>(nullptr, nullptr, nullptr, std::move(backend));

  AssetManager assetManager(renderer.get(),
                            false); // synchronous / no worker thread
  assetManager.setHotReloadEnabled(true);

  ShaderPaths paths;
  paths.vertexPath = vsPath.string();
  paths.fragmentPath = fsPath.string();

  ShaderHandle handle =
    assetManager.acquireShader(paths, AssetLoadMode::Synchronous);
  spTrue(handle.isValid(), "Acquired shader synchronously");
  AssetStatus status1 = assetManager.getState(handle);
  spTrue(status1.state == AssetState::Ready, "Shader state is Ready");
  spTrue(status1.revision == 1, "Initial revision is 1");

  // Wait 550ms for nextHotReloadPoll window and filesystem timestamp
  // granularity
  std::this_thread::sleep_for(std::chrono::milliseconds(550));
  {
    std::ofstream out(incPath, std::ios::trunc);
    out << "// v2\n#define TEST_CONSTANT 200\n";
  }

  // Force time advancement for hot reload poll
  assetManager.pump();
  // Complete pending reloads
  assetManager.completePendingForTests();

  AssetStatus status2 = assetManager.getState(handle);
  spTrue(
    status2.revision >= 2,
    "Shader reloaded and revision incremented after included file changed");

  // Clean up
  std::filesystem::remove_all(tempDir, ec);
}

static int
runShaderPreprocessorCase(void (*testFunction)())
{
  g_sp_failures = 0;
  testFunction();
  return g_sp_failures;
}

void
registerShaderPreprocessorTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Shader.VirtualIncludeResolution", []() {
    return runShaderPreprocessorCase(testVirtualIncludeResolution);
  });
  registry.add("Illumo.Shader.DiskIncludeResolution", []() {
    return runShaderPreprocessorCase(testDiskIncludeResolution);
  });
  registry.add("Illumo.Shader.PragmaOnceAndCircularDetection", []() {
    return runShaderPreprocessorCase(testPragmaOnceAndCircularDetection);
  });
  registry.add("Illumo.Shader.VersionPreservationAndDefines", []() {
    return runShaderPreprocessorCase(testVersionPreservationAndDefines);
  });
  registry.add("Illumo.Shader.LineDirectives",
               []() { return runShaderPreprocessorCase(testLineDirectives); });
  registry.add("Illumo.Shader.AssetManagerHotReloadDependencies", []() {
    return runShaderPreprocessorCase(testAssetManagerHotReloadWithInclude);
  });
}
