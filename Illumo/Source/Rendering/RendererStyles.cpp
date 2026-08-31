#include <Illumo/Rendering/IShaderProgram.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/ShaderPreprocessor.h>
#include <Illumo/Services/Logger.h>

// Built-in style shader sources and enrollment (D-R14). Owned by Renderer;
// production drawables only bind styles and emit content tokens.

static const char* kUiTextVertexShader = R"(
#version 330 core
#include <illumo/screen_transform.glsl>
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
out vec4 ourColor;
uniform vec2 u_resolution;
uniform vec2 u_scale;
uniform vec2 u_position;
void main() {
    gl_Position = illumoScaledScreenToClip(aPos.xy, u_scale, u_position, u_resolution, aPos.z);
    ourColor = aColor;
}
)";

static const char* kUiTextFragmentShader = R"(
#version 330 core
in vec4 ourColor;
out vec4 FragColor;
void main() {
    FragColor = ourColor;
}
)";

// Console batch uses absolute pixel positions (no u_position offset).
static const char* kConsoleVertexShader = R"(
#version 330 core
#include <illumo/screen_transform.glsl>
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
out vec4 ourColor;
uniform vec2 u_resolution;
uniform vec2 u_scale;
void main() {
    gl_Position = illumoScaledScreenToClip(aPos.xy, u_scale, vec2(0.0), u_resolution, aPos.z);
    ourColor = aColor;
}
)";

static const char* kConsoleFragmentShader = R"(
#version 330 core
in vec4 ourColor;
out vec4 FragColor;
void main() {
    FragColor = ourColor;
}
)";

// Shape primitives: world or overlay via uMVP (D-R21).
static const char* kShapeVertexShader = R"(
#version 330 core
#include <illumo/common.glsl>
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
out vec4 ourColor;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    ourColor = aColor;
}
)";

static const char* kShapeFragmentShader = R"(
#version 330 core
in vec4 ourColor;
out vec4 FragColor;
void main() {
    FragColor = ourColor;
}
)";

// Sprite primitives: same uMVP contract + texture sample with vertex tint.
static const char* kSpriteVertexShader = R"(
#version 330 core
#include <illumo/vertex_2d.glsl>
out vec4 ourColor;
out vec2 ourUv;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    ourColor = aColor;
    ourUv = aUv;
}
)";

static const char* kSpriteFragmentShader = R"(
#version 330 core
#include <illumo/sprite.glsl>
in vec4 ourColor;
in vec2 ourUv;
out vec4 FragColor;
uniform sampler2D uTexture;
void main() {
    FragColor = sampleSprite(uTexture, ourUv, ourColor);
}
)";

static void
fillCanvasPipeline(PipelineState& ps)
{
  ps.depthTestEnabled = false;
  ps.blendEnabled = false;
  ps.faceCullingEnabled = false;
  ps.primitives = Primitives::Triangles;
}

static void
fillUiBlendPipeline(PipelineState& ps)
{
  ps.depthTestEnabled = false;
  ps.blendEnabled = true;
  ps.blendSrc = BlendFactor::SrcAlpha;
  ps.blendDst = BlendFactor::OneMinusSrcAlpha;
  ps.faceCullingEnabled = false;
  ps.primitives = Primitives::Triangles;
}

void
Renderer::ensureBuiltinStyles()
{
  if (_builtinStylesReady || !_backend) {
    return;
  }

  // Canvas: file-backed shaders (same paths as historical Canvas enroll).
  {
    RenderStyle style;
    fillCanvasPipeline(style.pipeline);
    ShaderPaths paths;
    paths.vertexPath = "Shader/canvas_vertex.glsl";
    paths.fragmentPath = "Shader/canvas_frag.glsl";
    style.shaderHandle = enrollShader(paths);
    style.ready = style.shaderHandle.isValid();
    builtinStyleHandles[renderStyleIndex(RenderStyleId::Canvas)] =
      createStyle(style);
  }

  // UI text (GLString / SplashText / FPS overlay).
  {
    RenderStyle style;
    fillUiBlendPipeline(style.pipeline);
    ShaderSources sources;
    sources.vertexSource = kUiTextVertexShader;
    sources.fragmentSource = kUiTextFragmentShader;
    style.shaderHandle = enrollShader(sources);
    style.ready = style.shaderHandle.isValid();
    builtinStyleHandles[renderStyleIndex(RenderStyleId::UiText)] =
      createStyle(style);
  }

  // Console panel batch (CommandLine).
  {
    RenderStyle style;
    fillUiBlendPipeline(style.pipeline);
    ShaderSources sources;
    sources.vertexSource = kConsoleVertexShader;
    sources.fragmentSource = kConsoleFragmentShader;
    style.shaderHandle = enrollShader(sources);
    style.ready = style.shaderHandle.isValid();
    builtinStyleHandles[renderStyleIndex(RenderStyleId::Console)] =
      createStyle(style);
  }

  // Shape primitives (GameVisual).
  {
    RenderStyle style;
    fillUiBlendPipeline(style.pipeline);
    ShaderSources sources;
    sources.vertexSource = kShapeVertexShader;
    sources.fragmentSource = kShapeFragmentShader;
    style.shaderHandle = enrollShader(sources);
    style.ready = style.shaderHandle.isValid();
    builtinStyleHandles[renderStyleIndex(RenderStyleId::Shape)] =
      createStyle(style);
  }

  // Sprite primitives (GameVisual).
  {
    RenderStyle style;
    fillUiBlendPipeline(style.pipeline);
    ShaderSources sources;
    sources.vertexSource = kSpriteVertexShader;
    sources.fragmentSource = kSpriteFragmentShader;
    style.shaderHandle = enrollShader(sources);
    style.ready = style.shaderHandle.isValid();
    builtinStyleHandles[renderStyleIndex(RenderStyleId::Sprite)] =
      createStyle(style);
  }

  // 3D Lit Mesh / Solid Primitives (MeshVisual).
  {
    RenderStyle style;
    style.pipeline.depthTestEnabled = true;
    style.pipeline.blendEnabled = false;
    style.pipeline.faceCullingEnabled = false;
    style.pipeline.primitives = Primitives::Triangles;
    ShaderPaths paths;
    paths.vertexPath = "Shader/mesh_lit_vertex.glsl";
    paths.fragmentPath = "Shader/mesh_lit_frag.glsl";
    style.shaderHandle = enrollShader(paths);
    style.ready = style.shaderHandle.isValid();
    builtinStyleHandles[renderStyleIndex(RenderStyleId::LitMesh)] =
      createStyle(style);
  }

  // Shadow Depth pass (depth-only offscreen).
  {
    RenderStyle style;
    style.pipeline.depthTestEnabled = true;
    style.pipeline.blendEnabled = false;
    style.pipeline.faceCullingEnabled = false;
    style.pipeline.primitives = Primitives::Triangles;
    ShaderPaths paths;
    paths.vertexPath = "Shader/shadow_depth_vertex.glsl";
    paths.fragmentPath = "Shader/shadow_depth_frag.glsl";
    style.shaderHandle = enrollShader(paths);
    style.ready = style.shaderHandle.isValid();
    builtinStyleHandles[renderStyleIndex(RenderStyleId::ShadowDepth)] =
      createStyle(style);
  }

  _builtinStylesReady = true;
}

RenderStyleHandle
Renderer::createStyle(const RenderStyle& style)
{
  if (!style.ready || !style.shaderHandle.isValid()) {
    return RenderStyleHandle{};
  }
  RenderStyleHandle handle = styleHandles.allocate();
  RenderStyleEntry entry;
  entry.generation = handle.generation;
  entry.style = style;
  styleRegistry[handle.slot] = entry;
  return handle;
}

bool
Renderer::updateStyle(RenderStyleHandle handle, const RenderStyle& style)
{
  std::unordered_map<uint32_t, RenderStyleEntry>::iterator it =
    styleRegistry.find(handle.slot);
  if (!styleHandles.isCurrent(handle) || it == styleRegistry.end() ||
      it->second.generation != handle.generation || !style.ready ||
      !style.shaderHandle.isValid()) {
    Logger::LogWarning("updateStyle: invalid or stale style ignored");
    return false;
  }
  it->second.style = style;
  return true;
}

bool
Renderer::destroyStyle(RenderStyleHandle handle)
{
  std::unordered_map<uint32_t, RenderStyleEntry>::iterator it =
    styleRegistry.find(handle.slot);
  if (!styleHandles.isCurrent(handle) || it == styleRegistry.end() ||
      it->second.generation != handle.generation) {
    Logger::LogWarning("destroyStyle: stale style handle ignored");
    return false;
  }
  styleRegistry.erase(it);
  return styleHandles.release(handle);
}

const RenderStyle*
Renderer::getStyle(RenderStyleHandle handle) const
{
  std::unordered_map<uint32_t, RenderStyleEntry>::const_iterator it =
    styleRegistry.find(handle.slot);
  if (!styleHandles.isCurrent(handle) || it == styleRegistry.end() ||
      it->second.generation != handle.generation || !it->second.style.ready) {
    return nullptr;
  }
  return &it->second.style;
}

RenderStyle*
Renderer::getStyle(RenderStyleHandle handle)
{
  std::unordered_map<uint32_t, RenderStyleEntry>::iterator it =
    styleRegistry.find(handle.slot);
  if (!styleHandles.isCurrent(handle) || it == styleRegistry.end() ||
      it->second.generation != handle.generation || !it->second.style.ready) {
    return nullptr;
  }
  return &it->second.style;
}

bool
Renderer::bindStyle(RenderStyleHandle handle)
{
  const RenderStyle* style = getStyle(handle);
  if (!style) {
    Logger::LogWarning("bindStyle: invalid or stale style ignored");
    return false;
  }
  pushPipelineState(style->pipeline);
  pushSetShader(style->shaderHandle);
  return true;
}

RenderStyleHandle
Renderer::getBuiltinStyleHandle(RenderStyleId id) const
{
  const unsigned index = renderStyleIndex(id);
  if (index >= renderStyleCount()) {
    return RenderStyleHandle{};
  }
  return builtinStyleHandles[index];
}

const RenderStyle*
Renderer::getStyle(RenderStyleId id) const
{
  const unsigned index = renderStyleIndex(id);
  if (index >= renderStyleCount()) {
    return nullptr;
  }
  return getStyle(builtinStyleHandles[index]);
}

RenderStyle*
Renderer::getStyle(RenderStyleId id)
{
  ensureBuiltinStyles();
  const unsigned index = renderStyleIndex(id);
  if (index >= renderStyleCount()) {
    return nullptr;
  }
  return getStyle(builtinStyleHandles[index]);
}

bool
Renderer::bindStyle(RenderStyleId id)
{
  ensureBuiltinStyles();
  return bindStyle(getBuiltinStyleHandle(id));
}
