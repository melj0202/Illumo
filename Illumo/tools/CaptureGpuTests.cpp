#include "../Source/Engine/ProfilerOverlay.h"
#include <GL/glew.h>
#include <Illumo/Rendering/Font.h>
#include <Illumo/Rendering/FrameCapture.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/Primitives/MeshVisual.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Scene/SceneGraph.h>
#include <Illumo/Scene/SceneGraphDrawable.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

static FrameCaptureResult
captureSceneShadows(bool graphPath, bool shadows)
{
  FrameCaptureOptions options;
  options.width = 256;
  options.height = 192;
  return FrameCapture::render(
    options,
    [graphPath,
     shadows](Renderer& renderer, Camera& camera, std::string& error) {
      MeshVisual floor, cube;
      floor.setLightingEnabled(true);
      floor.setShadowsEnabled(shadows);
      cube.setLightingEnabled(true);
      cube.setShadowsEnabled(shadows);
      floor.addSolidCube(Vector3(0, -1, 0),
                         Vector3(4, 0.1f, 4),
                         ColorRgba{ 180, 180, 180, 255 });
      cube.addSolidCube(
        Vector3(0, 0, 0), Vector3(0.6f), ColorRgba{ 210, 110, 65, 255 });
      SceneGraph graph;
      SceneGraphDrawable drawable(graph);
      Scene scene(renderer.getWindow(), &camera);
      if (graphPath) {
        graph.addAttachment(graph.createNode(), &floor);
        graph.addAttachment(graph.createNode(), &cube);
        scene.AddDrawable(&drawable);
      } else {
        scene.AddDrawable(&floor);
        scene.AddDrawable(&cube);
      }
      class ShadowProbe : public DrawableBase
      {
      public:
        bool active = false;
        void Draw() override {}
        bool AppendCommands(Renderer* value) override
        {
          active = value->getShadowFrameContext().active;
          return true;
        }
      } probe;
      scene.AddDrawable(&probe);
      renderer.RenderScene(&scene, &camera);
      if (shadows && !probe.active) {
        error = "Scene shadow pass did not activate";
        return false;
      }
      renderer.SubmitOnly();
      if (graphPath && graph.getStatistics().extractions != 1) {
        error = "Shadow frame extracted the graph more than once";
        return false;
      }
      return true;
    });
}

static bool
testTextureUploads(Renderer& renderer, Camera&, std::string& error)
{
  std::vector<unsigned char> pixels(256u * 256u * 4u, 17u);
  const TextureHandle first =
    renderer.enrollTexture(pixels.data(), 256, 256, 4);
  const TextureHandle second =
    renderer.enrollTexture(pixels.data(), 256, 256, 4);
  std::vector<unsigned char> source(300u * 256u * 4u, 251u);
  for (std::size_t row = 0; row < 256u; ++row) {
    std::fill_n(source.data() + row * 300u * 4u,
                256u * 4u,
                static_cast<unsigned char>(20u + row % 128u));
  }
  renderer.pushSetTexture(first, 0);
  renderer.pushSetTexture(second, 1);
  // Exercise full PBO upload with padded rows, then cache-hit rebinding on 1.
  renderer.pushUpdateTexture(first, 0, 0, 256, 256, 4, source.data(), 300);
  renderer.pushSetTexture(second, 1);
  renderer.SubmitOnly();
  renderer.getBackend()->ClearCommandQueue();
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  if (!std::all_of(pixels.begin(), pixels.end(), [](unsigned char value) {
        return value == 17u;
      })) {
    error = "Upload corrupted the cached binding on texture unit one";
    return false;
  }
  glActiveTexture(GL_TEXTURE0);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  for (std::size_t index = 0; index < pixels.size(); ++index) {
    if (pixels[index] !=
        static_cast<unsigned char>(20u + (index / 1024u) % 128u)) {
      error = "Full padded PBO upload pixels differ";
      return false;
    }
  }
  std::fill(source.begin(), source.end(), 250u);
  for (std::size_t row = 0; row < 100u; ++row) {
    std::fill_n(source.data() + row * 300u * 4u,
                200u * 4u,
                static_cast<unsigned char>(129u + row));
  }
  renderer.pushUpdateTexture(first, 7, 9, 200, 100, 4, source.data(), 300);
  renderer.SubmitOnly();
  renderer.getBackend()->ClearCommandQueue();
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  for (int y = 0; y < 256; ++y) {
    for (int x = 0; x < 256; ++x) {
      const unsigned char expected = static_cast<unsigned char>(
        x >= 7 && x < 207 && y >= 9 && y < 109 ? 129 + y - 9 : 20 + y % 128);
      for (std::size_t channel = 0; channel < 4u; ++channel) {
        if (pixels[(static_cast<std::size_t>(y) * 256u +
                    static_cast<std::size_t>(x)) *
                     4u +
                   channel] != expected) {
          error = "Partial packed PBO upload pixels differ";
          return false;
        }
      }
    }
  }
  // Nondefault unpack state must not affect a direct upload or be lost.
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 19);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, 3);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 2);
  renderer.pushUpdateTexture(first, 255, 255, 1, 1, 4, source.data(), 0);
  renderer.SubmitOnly();
  renderer.getBackend()->ClearCommandQueue();
  GLint rowLength = 0;
  GLint skipRows = 0;
  GLint skipPixels = 0;
  glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowLength);
  glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skipRows);
  glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skipPixels);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  if (rowLength != 19 || skipRows != 3 || skipPixels != 2 ||
      pixels.back() != 129u) {
    error = "Direct upload did not preserve pixels/unpack state";
    return false;
  }
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
  renderer.destroyTexture(first);
  renderer.destroyTexture(second);
  return glGetError() == GL_NO_ERROR;
}

static bool
testInvalidTextureUpload(Renderer& renderer, Camera&, std::string& error)
{
  std::vector<unsigned char> pixels(256u * 256u * 4u, 17u);
  const TextureHandle texture =
    renderer.enrollTexture(pixels.data(), 256, 256, 4);
  std::vector<unsigned char> replacement(pixels.size(), 99u);
  renderer.pushSetTexture(texture, 0);
  renderer.pushUpdateTexture(
    texture, 0, 1, 256, 256, 4, replacement.data(), 256);
  renderer.SubmitOnly();
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  if (!std::all_of(pixels.begin(), pixels.end(), [](unsigned char value) {
        return value == 17u;
      })) {
    error = "Rejected rectangle changed texture pixels";
  }
  renderer.destroyTexture(texture);
  return true;
}

static bool
testCubemapUploads(Renderer& renderer, Camera&, std::string& error)
{
  const std::array<std::array<unsigned char, 4>, 6> values = {
    { { 1, 2, 3, 4 },
      { 5, 6, 7, 8 },
      { 9, 10, 11, 12 },
      { 13, 14, 15, 16 },
      { 17, 18, 19, 20 },
      { 21, 22, 23, 24 } }
  };
  std::array<const unsigned char*, 6> faces;
  for (std::size_t index = 0; index < faces.size(); ++index) {
    faces[index] = values[index].data();
  }
  if (renderer.enrollCubemap(faces, 1, 1, 2).isValid()) {
    error = "Unsupported two-channel raw cubemap accepted";
    return false;
  }
  const TextureHandle cube = renderer.enrollCubemap(faces, 1, 1, 4);
  renderer.pushSetTexture(cube, 0);
  renderer.SubmitOnly();
  for (std::size_t index = 0; index < faces.size(); ++index) {
    std::array<unsigned char, 4> pixel{};
    glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<GLenum>(index),
                  0,
                  GL_RGBA,
                  GL_UNSIGNED_BYTE,
                  pixel.data());
    if (pixel != values[index]) {
      error = "Cubemap face pixels differ";
      return false;
    }
  }
  if (renderer.replaceTexture(
        cube, values[0].data(), 1, 1, 4, TextureOptions{})) {
    error = "Cubemap accepted a 2D replacement";
    return false;
  }
  for (std::size_t index = 0; index < faces.size(); ++index) {
    faces[index] = values[faces.size() - 1 - index].data();
  }
  if (!renderer.replaceCubemap(cube, faces, 1, 1, 4)) {
    error = "Valid cubemap replacement failed";
    return false;
  }
  GLint maximumCubeSize = 0;
  glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &maximumCubeSize);
  if (renderer.replaceCubemap(
        cube, faces, maximumCubeSize + 1, maximumCubeSize + 1, 4)) {
    error = "Oversize cubemap replacement accepted";
    return false;
  }
  renderer.pushSetTexture(cube, 0);
  renderer.SubmitOnly();
  for (std::size_t index = 0; index < faces.size(); ++index) {
    std::array<unsigned char, 4> pixel{};
    glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<GLenum>(index),
                  0,
                  GL_RGBA,
                  GL_UNSIGNED_BYTE,
                  pixel.data());
    if (pixel != values[faces.size() - 1 - index]) {
      error = "Cubemap replacement or failure preservation changed pixels";
      return false;
    }
  }
  renderer.destroyTexture(cube);
  return glGetError() == GL_NO_ERROR;
}

static FrameCaptureResult
captureProfiler(int width, int height, bool renderingGroup)
{
  FrameCaptureOptions options;
  options.width = width;
  options.height = height;
  return FrameCapture::render(
    options,
    [width, height, renderingGroup](
      Renderer& renderer, Camera& camera, std::string&) {
      FrameProfiler profiler;
      ProfilerOverlay overlay(profiler);
      overlay.prepare(&renderer, renderer.getWindow(), &camera);
      overlay.setEnabled(true);
      for (size_t frame = 0; frame < FrameProfiler::kWindowFrames; ++frame) {
        const FrameProfiler::TimePoint start{};
        profiler.beginFrame(start);
        profiler.mark(FramePhase::Input, start);
        profiler.mark(FramePhase::ProductUpdate,
                      start + std::chrono::milliseconds(1));
        profiler.mark(FramePhase::ScenePreparation,
                      start + std::chrono::milliseconds(3));
        profiler.mark(FramePhase::Assets, start + std::chrono::milliseconds(4));
        profiler.mark(FramePhase::Commands,
                      start + std::chrono::milliseconds(5));
        profiler.mark(FramePhase::Presentation,
                      start + std::chrono::milliseconds(8));
        profiler.mark(FramePhase::Pacing,
                      start + std::chrono::milliseconds(15));
        profiler.endFrame(start + std::chrono::milliseconds(16));
      }
      if (renderingGroup) {
        overlay.handleKey(KeyCode::Num2, InputAction::Press, false, false);
      }
      overlay.update(
        0.0, static_cast<float>(width), static_cast<float>(height));
      renderer.pushClearScreen(0.04f, 0.05f, 0.07f, 1.0f);
      Scene scene(renderer.getWindow(), &camera);
      scene.AddDrawable(&overlay.visual(), RenderLayerId::Debug);
      renderer.RenderScene(&scene, &camera);
      renderer.SubmitOnly();
      return true;
    });
}

int
main(int argc, char** argv)
{
  Logger::setConsoleToStderr(true);
  if (argc == 3 && std::string(argv[1]) == "--profiler-preview") {
    const FrameCaptureResult preview = captureProfiler(800, 600, false);
    std::string error;
    if (!preview.success() ||
        !FrameCapture::savePng(argv[2], preview.image, &error)) {
      std::cerr << preview.error << error << '\n';
      return 1;
    }
    return 0;
  }
  if (argc != 1) {
    std::cerr
      << "Usage: IllumoCaptureGpuTests [--profiler-preview output.png]\n";
    return 2;
  }
  FrameCaptureOptions options;
  options.width = 32;
  options.height = 32;
  int failures = 0;
  const FrameCaptureResult directShadows = captureSceneShadows(false, true);
  const FrameCaptureResult sceneShadows = captureSceneShadows(true, true);
  const FrameCaptureResult unshadowed = captureSceneShadows(true, false);
  if (!directShadows.success() || !sceneShadows.success() ||
      !unshadowed.success() ||
      directShadows.image.pixels != sceneShadows.image.pixels ||
      sceneShadows.image.pixels == unshadowed.image.pixels) {
    std::cerr << "Scene snapshot shadow pixel parity failed: "
              << directShadows.error << "; " << sceneShadows.error << "; "
              << unshadowed.error << '\n';
    ++failures;
  } else {
    std::cout << "Scene snapshot/direct shadow pixels identical; shadows "
                 "change pixels\n";
  }

  for (int view = 0; view < 2; ++view) {
    const FrameCaptureResult chart =
      captureProfiler(view == 0 ? 800 : 320, view == 0 ? 600 : 240, view != 0);
    size_t greenPixels = 0;
    size_t purplePixels = 0;
    for (size_t i = 0; i + 3 < chart.image.pixels.size(); i += 4) {
      greenPixels += chart.image.pixels[i] == 185 &&
                         chart.image.pixels[i + 1] == 224 &&
                         chart.image.pixels[i + 2] == 91
                       ? 1
                       : 0;
      purplePixels += chart.image.pixels[i] == 146 &&
                          chart.image.pixels[i + 1] == 116 &&
                          chart.image.pixels[i + 2] == 245
                        ? 1
                        : 0;
    }
    if (!chart.success() || greenPixels < 100 || purplePixels < 100) {
      std::cerr << "Profiler geometry capture failed: " << chart.error << '\n';
      ++failures;
    }
  }
  std::shared_ptr<Font> persistentFont = Font::getDefaultFont();
  std::vector<unsigned char> previousTextPixels;
  for (int capture = 0; capture < 3; ++capture) {
    const FrameCaptureResult text = FrameCapture::render(
      options,
      [&persistentFont](Renderer& renderer, Camera&, std::string& error) {
        renderer.pushClearScreen(0, 0, 0, 1);
        GameVisual visual;
        visual.setWindow(renderer.getWindow());
        visual.prepare(&renderer);
        visual.addText("A", 2, 2, 24, ColorRgba{ 255, 255, 255, 255 });
        if (!persistentFont || !visual.AppendCommands(&renderer)) {
          error = "Text capture failed to emit font draw";
          return false;
        }
        renderer.SubmitOnly();
        return true;
      });
    bool visible = false;
    for (size_t pixel = 0; pixel + 3 < text.image.pixels.size(); pixel += 4) {
      visible = visible || text.image.pixels[pixel] > 64;
    }
    if (!text.success() || !visible ||
        (capture != 0 && text.image.pixels != previousTextPixels)) {
      std::cerr << "Successive font capture failed: " << text.error << '\n';
      ++failures;
    }
    previousTextPixels = text.image.pixels;
  }
  const FrameCaptureResult clears = FrameCapture::render(
    options, [](Renderer& renderer, Camera& camera, std::string& error) {
      PooledRenderTargetDesc desc;
      desc.name = "clear-semantics";
      desc.windowRelative = false;
      desc.fixedWidth = 8;
      desc.fixedHeight = 8;
      desc.colorAttachments.push_back(FramebufferAttachmentDesc{});
      desc.depthStencilFormat = TextureFormat::Depth24;
      const PooledRenderTarget target = renderer.acquireRenderTarget(desc);
      if (!target.isValid()) {
        error = "Clear test target creation failed";
        return false;
      }
      for (int mask = 0; mask < 4; ++mask) {
        renderer.pushFramebuffer(target.fboHandle);
        renderer.pushClearColor(1, 0, 0, 1);
        renderer.pushClearDepth(0.75f);
        renderer.SubmitOnly();
        Scene scene(nullptr, &camera);
        RenderPassDesc pass;
        pass.useScreenTarget = false;
        pass.pooledTargetName = desc.name;
        pass.targetDesc = desc;
        pass.clear.clearColor = (mask & 1) != 0;
        pass.clear.clearDepth = (mask & 2) != 0;
        pass.clear.clearColorValue = { 0, 1, 0, 1 };
        pass.clear.clearDepthValue = 0.25f;
        scene.SetLayerPasses(RenderLayerId::World, { pass });
        renderer.RenderScene(&scene, &camera);
        renderer.pushFramebuffer(target.fboHandle);
        renderer.SubmitOnly();
        std::array<unsigned char, 4> color{};
        float depth = 0;
        glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, color.data());
        glReadPixels(0, 0, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
        const bool colorCorrect = (mask & 1) != 0
                                    ? color[0] == 0 && color[1] == 255
                                    : color[0] == 255 && color[1] == 0;
        const float expectedDepth = (mask & 2) != 0 ? 0.25f : 0.75f;
        if (!colorCorrect || std::abs(depth - expectedDepth) > 0.00001f) {
          error = "Pass clear mask/value altered the wrong buffer";
          return false;
        }
      }
      renderer.pushClearDepth();
      renderer.SubmitOnly();
      float depth = 0;
      glReadPixels(0, 0, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
      if (std::abs(depth - 1.0f) > 0.00001f) {
        error = "Default depth clear inherited a prior custom value";
        return false;
      }
      renderer.pushFramebuffer(FramebufferHandle{});
      renderer.SubmitOnly();
      return glGetError() == GL_NO_ERROR;
    });
  if (!clears.success()) {
    std::cerr << "Clear mask/value checks: " << clears.error << '\n';
    ++failures;
  }
  const FrameCaptureResult targets = FrameCapture::render(
    options, [](Renderer& renderer, Camera&, std::string& error) {
      PooledRenderTargetDesc desc;
      desc.name = "submission-target";
      desc.windowRelative = false;
      desc.fixedWidth = 8;
      desc.fixedHeight = 8;
      desc.colorAttachments.push_back(FramebufferAttachmentDesc{});
      const PooledRenderTarget target = renderer.acquireRenderTarget(desc);
      renderer.pushFramebuffer(target.fboHandle);
      renderer.pushViewport(1, 2, 4, 5);
      renderer.SubmitOnly();
      GLint bound = 0;
      glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &bound);
      if (!target.isValid() || bound == 0) {
        error = "Offscreen submission did not bind its target";
        return false;
      }
      renderer.pushFramebuffer(FramebufferHandle{});
      renderer.pushViewport(0, 0, 32, 32);
      renderer.SubmitOnly();
      glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &bound);
      std::array<GLint, 4> viewport{};
      glGetIntegerv(GL_VIEWPORT, viewport.data());
      if (bound != 0 || viewport != std::array<GLint, 4>{ 0, 0, 32, 32 }) {
        error = "New submission failed to restore screen framebuffer/viewport";
        return false;
      }
      return glGetError() == GL_NO_ERROR;
    });
  if (!targets.success()) {
    std::cerr << "Framebuffer restoration checks: " << targets.error << '\n';
    ++failures;
  }
  const FrameCaptureResult readback = FrameCapture::render(
    options, [](Renderer& renderer, Camera&, std::string& error) {
      IBackend* backend = renderer.getBackend();
      FramebufferDesc desc;
      desc.width = 16;
      desc.height = 8;
      desc.colorAttachments.push_back(FramebufferAttachmentDesc{});
      const FramebufferHandle target = backend->CreateFramebuffer(desc);
      GameVisual visual;
      visual.setWindow(renderer.getWindow());
      visual.setSpace(PrimitiveSpace::Pixels);
      visual.prepare(&renderer);
      visual.addFilledRect(0, 0, 16, 4, ColorRgba{ 255, 0, 0, 255 });
      if (!renderer.renderOffscreen(
            target, 16, 8, { &visual }, { 0.0f, 0.0f, 1.0f, 1.0f }, 1.0f)) {
        error = "Offscreen render failed";
        return false;
      }
      FrameReadback pixels;
      if (!backend->requestFramebufferReadback(1, target, 16, 8) ||
          !backend->takeFramebufferReadback(1, true, pixels) ||
          pixels.width != 16 || pixels.height != 8) {
        error = "Framebuffer readback failed: " + pixels.error;
        return false;
      }
      // Rows are top-down: the rectangle covers the top half.
      const unsigned char* top = pixels.pixels.data();
      const unsigned char* bottom = pixels.pixels.data() + 7 * 16 * 4;
      if (top[0] != 255 || top[2] != 0 || bottom[0] != 0 || bottom[2] != 255) {
        error = "Readback pixels or row order differ";
        return false;
      }
      backend->releaseReadbackStream(1);
      backend->DestroyFramebuffer(target);
      return glGetError() == GL_NO_ERROR;
    });
  if (!readback.success()) {
    std::cerr << "Framebuffer readback checks: " << readback.error << '\n';
    ++failures;
  }
  const FrameCaptureResult uploads =
    FrameCapture::render(options, testTextureUploads);
  if (!uploads.success()) {
    std::cerr << "Texture upload checks: " << uploads.error << '\n';
    ++failures;
  }
  const FrameCaptureResult bounds =
    FrameCapture::render(options, testInvalidTextureUpload);
  if (bounds.success() ||
      bounds.error.find("invalid rectangle") == std::string::npos) {
    std::cerr << "Texture bounds checks: " << bounds.error << '\n';
    ++failures;
  }
  const FrameCaptureResult cubes =
    FrameCapture::render(options, testCubemapUploads);
  if (!cubes.success()) {
    std::cerr << "Cubemap upload checks: " << cubes.error << '\n';
    ++failures;
  }
  const FrameCaptureResult allocation = FrameCapture::render(
    options, [](Renderer& renderer, Camera&, std::string& error) {
      IBackend* backend = renderer.getBackend();
      const unsigned char pixels[4] = { 255, 255, 255, 255 };
      const TextureHandle texture =
        backend->CreateTexture(pixels, 1, 1, 4, TextureOptions{});
      GLint maximum = 0;
      glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
      const TextureHandle oversized =
        backend->CreateTexture(pixels, maximum + 1, 1, 4, TextureOptions{});
      const TextureHandle invalidChannels =
        backend->CreateTexture(pixels, 1, 1, 2, TextureOptions{});
      const bool replaced =
        backend->ReplaceTexture(texture, pixels, 1, 1, 2, TextureOptions{});
      const MeshHandle mesh = renderer.enrollDynamicMesh(
        16, nullptr, 0, MeshVertexLayout::Pos3Color4U8);
      const MeshHandle empty = backend->CreateMesh(
        nullptr, 0, nullptr, 0, MeshVertexLayout::Pos3Color4U8, true);
      const bool replacedMesh = backend->ReplaceMesh(
        mesh, nullptr, 0, nullptr, 0, MeshVertexLayout::Pos3Color4U8, true);
      const bool valid = texture.isValid() && !oversized.isValid() &&
                         !invalidChannels.isValid() && !replaced &&
                         backend->IsTextureValid(texture) && mesh.isValid() &&
                         !empty.isValid() && !replacedMesh &&
                         backend->IsMeshValid(mesh);
      if (mesh.isValid())
        backend->DestroyMesh(mesh);
      if (texture.isValid())
        backend->DestroyTexture(texture);
      if (!valid)
        error =
          "Invalid allocation published a handle or replaced a good resource";
      return valid;
    });
  if (!allocation.success()) {
    std::cerr << "Allocation validation checks: " << allocation.error << '\n';
    ++failures;
  }
  const FrameCaptureResult vertexBounds = FrameCapture::render(
    options, [](Renderer& renderer, Camera&, std::string&) {
      const MeshHandle mesh = renderer.enrollDynamicMesh(
        16, nullptr, 0, MeshVertexLayout::Pos3Color4U8);
      const std::array<unsigned char, 17> bytes{};
      renderer.pushUpdateBuffer(mesh, 0, 17, bytes.data());
      renderer.SubmitOnly();
      renderer.destroyMesh(mesh);
      return true;
    });
  if (vertexBounds.success() ||
      vertexBounds.error.find("vertex capacity") == std::string::npos) {
    std::cerr << "Vertex bounds checks: " << vertexBounds.error << '\n';
    ++failures;
  }
  const FrameCaptureResult overflow = FrameCapture::render(
    options, [](Renderer& renderer, Camera&, std::string&) {
      for (int i = 0; i < 65537; ++i) {
        renderer.pushClearColor(0, 0, 0, 1);
      }
      renderer.SubmitOnly();
      renderer.getBackend()->ClearCommandQueue();
      return true;
    });
  if (overflow.success() ||
      overflow.error.find("safety ceiling") == std::string::npos) {
    std::cerr << "Overflow did not survive queue reset: " << overflow.error
              << '\n';
    ++failures;
  }
  const FrameCaptureResult invalid = FrameCapture::render(
    options, [](Renderer& renderer, Camera&, std::string&) {
      renderer.pushSetMesh(MeshHandle{});
      renderer.SubmitOnly();
      renderer.getBackend()->ClearCommandQueue();
      return true;
    });
  if (invalid.success() ||
      invalid.error.find("unknown mesh") == std::string::npos) {
    std::cerr << "Invalid handle failure lost: " << invalid.error << '\n';
    ++failures;
  }
  const FrameCaptureResult exception =
    FrameCapture::render(options, [](Renderer&, Camera&, std::string&) -> bool {
      throw std::runtime_error("producer exception");
    });
  if (exception.success() || exception.error != "producer exception") {
    ++failures;
  }
  const FrameCaptureResult strict = FrameCapture::render(
    options, [](Renderer& renderer, Camera&, std::string&) {
      renderer.reportFrameError("required attachment failed");
      return false;
    });
  if (strict.success() || strict.error != "required attachment failed") {
    ++failures;
  }
  const FrameCaptureResult recovered = FrameCapture::render(
    options, [](Renderer& renderer, Camera&, std::string&) {
      renderer.pushClearScreen(1, 0, 0, 1);
      renderer.SubmitOnly();
      return true;
    });
  if (!recovered.success() || recovered.image.pixels[0] != 255 ||
      recovered.image.pixels[1] != 0 || recovered.elapsedMilliseconds <= 0) {
    std::cerr << "Context cleanup/recreation failed: " << recovered.error
              << '\n';
    ++failures;
  }
  std::cout << "GPU backend failure and lifecycle checks: " << failures
            << " failures\n";
  return failures == 0 ? 0 : 1;
}
