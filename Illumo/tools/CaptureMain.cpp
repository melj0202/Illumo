#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/FrameCapture.h>
#include <Illumo/Rendering/ISceneRenderAttachment.h>
#include <Illumo/Rendering/MeshLoader.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Rendering/ShaderPreprocessor.h>
#include <Illumo/Scene/SceneGraph.h>
#include <Illumo/Scene/SceneGraphDrawable.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

#ifndef ILLUMO_CAPTURE_REVISION
#define ILLUMO_CAPTURE_REVISION "unknown"
#endif

struct CaptureVertex
{
  float x, y, z;
  unsigned char r, g, b, a;
};
static_assert(sizeof(CaptureVertex) == 16);

class CaptureFixture final
  : public DrawableBase
  , public ISceneRenderAttachment
{
public:
  explicit CaptureFixture(Renderer& renderer)
    : m_renderer(renderer)
  {
  }
  ~CaptureFixture() override
  {
    if (m_mesh.isValid()) {
      m_renderer.destroyMesh(m_mesh);
    }
    if (m_shader.isValid()) {
      m_renderer.destroyShader(m_shader);
    }
  }
  CaptureFixture(const CaptureFixture&) = delete;
  CaptureFixture& operator=(const CaptureFixture&) = delete;
  CaptureFixture(CaptureFixture&&) = delete;
  CaptureFixture& operator=(CaptureFixture&&) = delete;

  bool prepare(const MeshData& mesh,
               const ShaderPaths& paths,
               std::string& error)
  {
    PreprocessResult vertex = ShaderPreprocessor::ProcessFile(paths.vertexPath);
    PreprocessResult fragment =
      ShaderPreprocessor::ProcessFile(paths.fragmentPath);
    if (!vertex.success || !fragment.success) {
      error = !vertex.success ? vertex.errorMessage : fragment.errorMessage;
      return false;
    }
    m_shader = m_renderer.enrollShader(
      ShaderSources{ vertex.source, fragment.source, {} });
    if (!m_shader.isValid()) {
      error = "Shader compilation/link failed: " + paths.vertexPath + " / " +
              paths.fragmentPath;
      return false;
    }
    std::vector<CaptureVertex> vertices;
    vertices.reserve(mesh.vertices.size());
    for (const MeshVertex& value : mesh.vertices) {
      CaptureVertex packed{ value.position.x,
                            value.position.y,
                            value.position.z,
                            static_cast<unsigned char>(
                              std::clamp(value.color.r, 0.0f, 1.0f) * 255.0f),
                            static_cast<unsigned char>(
                              std::clamp(value.color.g, 0.0f, 1.0f) * 255.0f),
                            static_cast<unsigned char>(
                              std::clamp(value.color.b, 0.0f, 1.0f) * 255.0f),
                            255 };
      vertices.push_back(packed);
    }
    m_mesh = m_renderer.enrollMesh(vertices.data(),
                                   vertices.size() * sizeof(CaptureVertex),
                                   mesh.indices.data(),
                                   mesh.indices.size() * sizeof(uint32_t),
                                   MeshVertexLayout::Pos3Color4U8,
                                   false);
    m_indexCount = static_cast<unsigned int>(mesh.indices.size());
    if (!m_mesh.isValid()) {
      error = "Unable to enroll required fixture mesh";
      return false;
    }
    return true;
  }
  void setDirectTransform(const Matrix4& transform)
  {
    m_directTransform = transform;
  }
  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override
  {
    appendSceneCommands(renderer, m_directTransform);
    return true;
  }
  void appendSceneCommands(Renderer* renderer, const Matrix4& world) override
  {
    if (renderer != &m_renderer || !m_mesh.isValid() || !m_shader.isValid()) {
      m_renderer.reportFrameError("Invalid capture fixture resources");
      return;
    }
    const Renderer::FrameContext& frame = renderer->getFrameContext();
    if (!frame.active || !frame.hasWorldMvp) {
      renderer->reportFrameError(
        "Capture fixture requires the normal RenderScene frame context");
      return;
    }
    Matrix4 viewProjection(1.0f);
    for (size_t i = 0; i < 16; ++i) {
      (&viewProjection[0][0])[i] = frame.worldMvp[i];
    }
    const Matrix4 mvp = viewProjection * world;
    PipelineState state;
    state.depthTestEnabled = true;
    state.blendEnabled = false;
    state.faceCullingEnabled = false;
    renderer->pushPipelineState(state);
    renderer->pushSetShader(m_shader);
    renderer->pushSetMesh(m_mesh);
    renderer->pushUniformMat4("uMVP", &mvp[0][0]);
    renderer->pushDrawIndexed(m_indexCount);
  }

private:
  Renderer& m_renderer;
  MeshHandle m_mesh;
  ShaderHandle m_shader;
  unsigned int m_indexCount = 0;
  Matrix4 m_directTransform{ 1.0f };
};

static float
parseNumber(const std::string& value)
{
  size_t consumed = 0;
  const float number = std::stof(value, &consumed);
  if (consumed != value.size() || !std::isfinite(number)) {
    throw std::runtime_error("Expected a finite number: " + value);
  }
  return number;
}

static MeshData
cubeFixture()
{
  MeshData mesh;
  for (int i = 0; i < 8; ++i) {
    MeshVertex vertex;
    vertex.position = glm::vec3((i & 1) != 0 ? 1.0f : -1.0f,
                                (i & 2) != 0 ? 1.0f : -1.0f,
                                (i & 4) != 0 ? 1.0f : -1.0f);
    vertex.color = glm::vec4((i & 1) != 0 ? 1.0f : 0.2f,
                             (i & 2) != 0 ? 0.85f : 0.2f,
                             (i & 4) != 0 ? 1.0f : 0.2f,
                             1.0f);
    mesh.vertices.push_back(vertex);
  }
  mesh.indices = { 0, 2, 3, 0, 3, 1, 4, 5, 7, 4, 7, 6, 0, 1, 5, 0, 5, 4,
                   2, 6, 7, 2, 7, 3, 0, 4, 6, 0, 6, 2, 1, 3, 7, 1, 7, 5 };
  return mesh;
}

int
main(int argc, char** argv)
{
  Logger::setConsoleToStderr(true);
  nlohmann::json diagnostic{ { "success", false },
                             { "backend", "opengl" },
                             { "engine_revision", ILLUMO_CAPTURE_REVISION },
                             { "stage", "arguments" },
                             { "effects", "unlit; no temporal effects" },
                             { "runtime_started", false } };
  try {
    FrameCaptureOptions options;
    std::filesystem::path output;
    std::filesystem::path meshPath;
    std::string mode = "direct";
    float rotation = 25.0f;
    bool inspectOnly = false;
    const std::filesystem::path executableDir =
      std::filesystem::absolute(argv[0]).parent_path();
    ShaderPaths shaders{
      (executableDir / "Shader/capture_vertex.glsl").string(),
      (executableDir / "Shader/capture_frag.glsl").string(),
      {}
    };
    for (int i = 1; i < argc; ++i) {
      const std::string option = argv[i];
      if (option == "--help") {
        std::cout
          << "IllumoCapture --output image.png [--mode direct|scene] [--width "
             "N --height N]\n"
             "  [--eye X Y Z --target X Y Z --fov DEGREES --rotation DEGREES]\n"
             "  [--mesh file.obj] [--vertex-shader file --fragment-shader "
             "file] [--backend opengl]\n"
             "IllumoCapture --inspect-mesh file.obj (CPU only)\n"
             "Outputs JSON diagnostics; existing image files are never "
             "overwritten.\n";
        return 0;
      }
      if (i + 1 >= argc) {
        throw std::runtime_error("Missing value for " + option);
      }
      const std::string value = argv[++i];
      if (option == "--output") {
        output = value;
      } else if (option == "--mode") {
        mode = value;
      } else if (option == "--mesh" || option == "--inspect-mesh") {
        meshPath = value;
        inspectOnly = option == "--inspect-mesh";
      } else if (option == "--vertex-shader") {
        shaders.vertexPath = value;
      } else if (option == "--fragment-shader") {
        shaders.fragmentPath = value;
      } else if (option == "--rotation") {
        rotation = parseNumber(value);
      } else if (option == "--fov") {
        options.verticalFovDegrees = parseNumber(value);
      } else if (option == "--backend") {
        if (value != "opengl") {
          throw std::runtime_error("Unsupported backend: " + value);
        }
      } else if (option == "--width" || option == "--height") {
        const float dimension = parseNumber(value);
        if (dimension < 1.0f || dimension > 4096.0f ||
            std::floor(dimension) != dimension) {
          throw std::runtime_error(
            "Dimensions must be integers within 1..4096");
        }
        if (option == "--width") {
          options.width = static_cast<int>(dimension);
        } else {
          options.height = static_cast<int>(dimension);
        }
      } else if (option == "--eye" || option == "--target") {
        if (i + 2 >= argc) {
          throw std::runtime_error("Camera vectors need three coordinates");
        }
        std::array<float, 3> vector{ parseNumber(value),
                                     parseNumber(argv[i + 1]),
                                     parseNumber(argv[i + 2]) };
        i += 2;
        if (option == "--eye") {
          options.eye = vector;
        } else {
          options.target = vector;
        }
      } else {
        throw std::runtime_error("Unknown option: " + option);
      }
    }
    if (mode != "direct" && mode != "scene") {
      throw std::runtime_error("Mode must be direct or scene");
    }
    if (std::abs(rotation) > 360000.0f) {
      throw std::runtime_error("Rotation must be within +/-360000 degrees");
    }
    const std::string validation = FrameCapture::validate(options);
    if (!validation.empty()) {
      throw std::runtime_error(validation);
    }
    if (!inspectOnly && output.empty()) {
      throw std::runtime_error("--output is required");
    }
    if (!inspectOnly &&
        (std::filesystem::exists(output) ||
         std::filesystem::exists(output.string() + ".partial"))) {
      throw std::runtime_error("Output already exists");
    }
    diagnostic["mode"] = mode;
    diagnostic["width"] = options.width;
    diagnostic["height"] = options.height;
    diagnostic["eye"] = options.eye;
    diagnostic["target"] = options.target;
    diagnostic["fov"] = options.verticalFovDegrees;
    diagnostic["rotation"] = rotation;
    diagnostic["mesh"] =
      meshPath.empty() ? "builtin:cube-v1" : meshPath.string();
    diagnostic["vertex_shader"] = shaders.vertexPath;
    diagnostic["fragment_shader"] = shaders.fragmentPath;
    diagnostic["stage"] = "assets";
    MeshData mesh;
    if (!meshPath.empty()) {
      MeshLoadResult loaded = MeshLoader::loadFromFile(meshPath.string());
      if (!loaded.success) {
        throw std::runtime_error(loaded.error);
      }
      diagnostic["asset_warning"] = loaded.warning;
      mesh = std::move(loaded.mesh);
    } else {
      mesh = cubeFixture();
    }
    diagnostic["vertices"] = mesh.vertices.size();
    diagnostic["indices"] = mesh.indices.size();
    if (inspectOnly) {
      diagnostic["backend"] = nullptr;
      diagnostic["stage"] = "complete";
      diagnostic["success"] = true;
      std::cout << diagnostic.dump(2) << '\n';
      return 0;
    }
    if (mesh.vertices.empty() || mesh.indices.empty() ||
        mesh.vertices.size() > 1000000 || mesh.indices.size() > 3000000) {
      throw std::runtime_error(
        "Capture mesh is empty or exceeds fixture limits");
    }
    const Transform3D transform =
      Transform3D::fromEuler(0.0f, glm::radians(rotation), 0.0f);
    FrameCaptureResult result = FrameCapture::render(
      options, [&](Renderer& renderer, Camera& camera, std::string& error) {
        CaptureFixture fixture(renderer);
        if (!fixture.prepare(mesh, shaders, error)) {
          return false;
        }
        Scene frame(renderer.getWindow(), &camera);
        SceneGraph graph;
        SceneGraphDrawable graphDrawable(graph);
        if (mode == "scene") {
          const SceneNodeHandle node = graph.createNode();
          if (!graph.setLocalTransform(node, transform) ||
              !graph.setRenderAttachment(node, &fixture)) {
            error = "Unable to assemble scene fixture";
            return false;
          }
          frame.AddDrawable(&graphDrawable);
        } else {
          fixture.setDirectTransform(transform.toMatrix());
          frame.AddDrawable(&fixture);
        }
        renderer.RenderScene(&frame, &camera);
        return renderer.frameError().empty();
      });
    diagnostic["stage"] = result.stage;
    diagnostic["elapsed_ms"] = result.elapsedMilliseconds;
    if (!result.success()) {
      diagnostic["error"] = result.error;
      std::cout << diagnostic.dump(2) << '\n';
      return 1;
    }
    diagnostic["stage"] = "output";
    std::string error;
    if (!FrameCapture::savePng(output, result.image, &error)) {
      throw std::runtime_error(error);
    }
    diagnostic["output"] = output.string();
    diagnostic["stage"] = "complete";
    diagnostic["success"] = true;
    std::cout << diagnostic.dump(2) << '\n';
    return 0;
  } catch (const std::exception& exception) {
    diagnostic["error"] = exception.what();
    std::cout << diagnostic.dump(2) << '\n';
    return diagnostic["stage"] == "arguments" ? 2 : 1;
  }
}
