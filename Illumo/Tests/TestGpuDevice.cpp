// Headless checks of GpuDevice, the token interpreter and resource front ends
// Vulkan and Direct3D 12 share (docs/gpu-backend-unification-plan.md). A
// recording device stands in for the API: the checks see which hooks the
// base calls, with what, and which tokens it refuses. Pixels are covered by
// IllumoGpuTests.

#include "Rendering/Gpu/GpuDevice.h"
#include <Illumo/Rendering/CommandQueue.h>
#include <Illumo/Rendering/RenderCommand.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

struct FakeMesh : GpuMesh
{
  std::vector<unsigned char> vertices;
  std::vector<unsigned char> indices;
};

struct FakeProgram : GpuProgram
{};

struct FakeTexture : GpuTexture
{};

struct FakeBuffer : GpuBuffer
{
  std::vector<unsigned char> bytes;
};

struct RecordedDraw
{
  int kind = -1;
  unsigned count = 0;
  unsigned first = 0;
  unsigned instances = 0;
  const GpuProgram* program = nullptr;
  const GpuMesh* mesh = nullptr;
};

// A GpuDevice whose hooks record what they were asked to do.
class RecordingDevice final : public GpuDevice
{
public:
  RecordingDevice() = default;
  ~RecordingDevice() override { shutdown(); }
  RecordingDevice(const RecordingDevice&) = delete;
  RecordingDevice& operator=(const RecordingDevice&) = delete;
  RecordingDevice(RecordingDevice&&) = delete;
  RecordingDevice& operator=(RecordingDevice&&) = delete;

  const char* apiName() const override { return "Recording"; }
  bool initialize(IRenderWindow* /*window*/,
                  bool /*present*/,
                  std::string* /*error*/) override
  {
    return true;
  }
  void shutdown() override { releaseResources(); }
  void beginFrame() override {}
  void endFrame() override {}
  void plotFrameStats(size_t /*commandQueueHighWater*/) const override {}
  FrameReadback readBackbuffer(int /*width*/, int /*height*/) override
  {
    return {};
  }
  bool requestFramebufferReadback(std::uint32_t /*stream*/,
                                  FramebufferHandle /*framebuffer*/,
                                  int /*width*/,
                                  int /*height*/) override
  {
    return false;
  }
  bool takeFramebufferReadback(std::uint32_t /*stream*/,
                               bool /*wait*/,
                               FrameReadback& /*out*/) override
  {
    return false;
  }
  void releaseReadbackStream(std::uint32_t /*stream*/) override {}

  // What the hooks saw.
  bool failBuilds = false;
  int meshReleases = 0;
  int programReleases = 0;
  int textureReleases = 0;
  int bufferReleases = 0;
  int clears = 0;
  int textureUploads = 0;
  std::vector<FramebufferHandle> releasedTargets;
  std::vector<RecordedDraw> draws;

  const FakeMesh* mesh(MeshHandle handle) const
  {
    return static_cast<const FakeMesh*>(findMesh(handle));
  }
  const FakeBuffer* buffer(BufferHandle handle) const
  {
    return static_cast<const FakeBuffer*>(findBuffer(handle));
  }

protected:
  std::unique_ptr<GpuMesh> buildMesh(size_t vertexSize,
                                     size_t indexSize) override
  {
    if (failBuilds) {
      return nullptr;
    }
    std::unique_ptr<FakeMesh> mesh = std::make_unique<FakeMesh>();
    mesh->vertices.resize(vertexSize);
    mesh->indices.resize(indexSize);
    return mesh;
  }
  void releaseMesh(GpuMesh& /*mesh*/) override { ++meshReleases; }
  std::unique_ptr<GpuProgram> buildProgram(const ShaderSources& sources,
                                           std::string* error) override
  {
    if (failBuilds || sources.vertexSource.empty()) {
      *error = "no vertex source";
      return nullptr;
    }
    return std::make_unique<FakeProgram>();
  }
  void releaseProgram(GpuProgram& /*program*/) override { ++programReleases; }
  std::unique_ptr<GpuTexture> buildTexture(
    const unsigned char* /*data*/,
    int /*width*/,
    int /*height*/,
    int channels,
    const TextureOptions& /*options*/) override
  {
    if (failBuilds) {
      return nullptr;
    }
    std::unique_ptr<FakeTexture> texture = std::make_unique<FakeTexture>();
    texture->channels = channels;
    texture->storageBytes = channels == 1 ? 1 : 4;
    return texture;
  }
  std::unique_ptr<GpuTexture> buildCubemap(
    const std::array<const unsigned char*, 6>& /*faces*/,
    int /*width*/,
    int /*height*/,
    int channels) override
  {
    std::unique_ptr<FakeTexture> texture = std::make_unique<FakeTexture>();
    texture->channels = channels;
    texture->cubemap = true;
    return texture;
  }
  std::unique_ptr<GpuTexture> buildRenderTarget(int /*width*/,
                                                int /*height*/,
                                                TextureFormat format,
                                                TextureFilter /*filter*/,
                                                TextureWrap /*wrap*/) override
  {
    std::unique_ptr<FakeTexture> texture = std::make_unique<FakeTexture>();
    texture->renderTarget = true;
    texture->format = format;
    const bool depth = format == TextureFormat::Depth24 ||
                       format == TextureFormat::Depth24Stencil8;
    texture->storageBytes = depth ? 0 : 4;
    return texture;
  }
  void releaseTexture(GpuTexture& /*texture*/) override { ++textureReleases; }
  std::unique_ptr<GpuBuffer> buildBuffer(size_t capacityBytes) override
  {
    std::unique_ptr<FakeBuffer> buffer = std::make_unique<FakeBuffer>();
    buffer->bytes.resize(capacityBytes);
    return buffer;
  }
  void releaseBuffer(GpuBuffer& /*buffer*/) override { ++bufferReleases; }
  unsigned maxColorAttachments() const override { return 8; }
  void releaseFramebufferTarget(FramebufferHandle handle) override
  {
    releasedTargets.push_back(handle);
  }

  void clear(bool /*color*/,
             bool /*depth*/,
             bool /*stencil*/,
             bool /*mrtAware*/,
             const CmdClearColor& /*value*/,
             float /*depthValue*/) override
  {
    ++clears;
  }
  void recordDraw(DrawKind kind,
                  unsigned count,
                  unsigned first,
                  unsigned instances,
                  GpuProgram& program,
                  GpuMesh& mesh) override
  {
    RecordedDraw draw;
    draw.kind = static_cast<int>(kind);
    draw.count = count;
    draw.first = first;
    draw.instances = instances;
    draw.program = &program;
    draw.mesh = &mesh;
    draws.push_back(draw);
  }
  bool uploadTexture(GpuTexture& /*texture*/,
                     const CmdUpdateTexture& /*update*/,
                     int /*channels*/) override
  {
    ++textureUploads;
    return true;
  }
  void uploadMeshData(GpuMesh& mesh,
                      bool indices,
                      size_t offset,
                      const void* data,
                      size_t size) override
  {
    FakeMesh& fake = static_cast<FakeMesh&>(mesh);
    std::vector<unsigned char>& target = indices ? fake.indices : fake.vertices;
    std::memcpy(target.data() + offset, data, size);
  }
  void uploadBufferData(GpuBuffer& buffer,
                        size_t offset,
                        const void* data,
                        size_t size) override
  {
    FakeBuffer& fake = static_cast<FakeBuffer&>(buffer);
    std::memcpy(fake.bytes.data() + offset, data, size);
  }
};

static RenderCommand
token(CommandType type)
{
  RenderCommand command{};
  command.commandType = type;
  return command;
}

static void
submit(RecordingDevice& device, const std::vector<RenderCommand>& commands)
{
  CommandQueue queue;
  for (const RenderCommand& command : commands) {
    queue.Submit(command);
  }
  device.executeQueue(queue);
}

static RenderCommand
viewport(int width, int height)
{
  RenderCommand command = token(CommandType::SetViewport);
  command.viewport = CmdViewport{ 0, 0, width, height };
  return command;
}

static RenderCommand
bindShader(ShaderHandle handle)
{
  RenderCommand command = token(CommandType::SetShader);
  command.bindShader.handle = handle;
  return command;
}

static RenderCommand
bindMesh(MeshHandle handle)
{
  RenderCommand command = token(CommandType::SetMesh);
  command.bindMesh.handle = handle;
  return command;
}

static RenderCommand
drawArrays(unsigned count, unsigned first)
{
  RenderCommand command = token(CommandType::Draw);
  command.draw.elementCount = count;
  command.draw.first = first;
  return command;
}

static RenderCommand
drawIndexed(unsigned count, unsigned first)
{
  RenderCommand command = token(CommandType::DrawIndexed);
  command.drawIndexed.elementCount = count;
  command.drawIndexed.firstIndex = first;
  return command;
}

static int
testResources()
{
  TestCounters counters;
  testSection("GpuDevice: meshes are enrolled as GLMesh enrolls them");
  RecordingDevice device;
  // Pos3Color3Uv2: 32 bytes a vertex.
  const std::vector<float> vertices(24, 1.0f);
  const unsigned char byteIndices[6] = { 0, 1, 2, 2, 1, 0 };
  testTrue(counters,
           !device
              .createMesh(
                nullptr, 0, nullptr, 0, MeshVertexLayout::Pos3Color3Uv2, false)
              .isValid(),
           "a mesh without vertex bytes is refused");
  testTrue(counters,
           !device
              .createMesh(
                nullptr, 96, nullptr, 0, MeshVertexLayout::Pos3Color3Uv2, false)
              .isValid(),
           "a static mesh without vertices is refused");
  const MeshHandle mesh = device.createMesh(vertices.data(),
                                            96,
                                            byteIndices,
                                            sizeof(byteIndices),
                                            MeshVertexLayout::Pos3Color3Uv2,
                                            false);
  testTrue(counters, device.isMeshValid(mesh), "a mesh is created");
  const FakeMesh* record = device.mesh(mesh);
  testTrue(counters,
           record != nullptr && record->vertexCapacity == 96 &&
             record->indexCapacity == 6 && record->indexCount == 1,
           "the record holds the capacities and the index count");
  unsigned int firstIndex = 0xffffffffu;
  if (record != nullptr && record->indices.size() >= sizeof(firstIndex)) {
    std::memcpy(&firstIndex, record->indices.data(), sizeof(firstIndex));
  }
  testTrue(counters,
           firstIndex == 0u,
           "byte indices are widened before the enrolled bytes upload");
  testTrue(counters,
           !device.replaceMesh(MeshHandle{},
                               vertices.data(),
                               96,
                               nullptr,
                               0,
                               MeshVertexLayout::Pos3Color3Uv2,
                               false),
           "replacing a stale mesh fails");
  testTrue(counters,
           device.replaceMesh(mesh,
                              vertices.data(),
                              96,
                              nullptr,
                              0,
                              MeshVertexLayout::Pos3Color3Uv2,
                              false) &&
             device.meshReleases == 1 && device.isMeshValid(mesh),
           "a replacement releases the old mesh and keeps the handle");
  testTrue(counters,
           device.destroyMesh(mesh) && !device.isMeshValid(mesh) &&
             !device.destroyMesh(mesh) && device.meshReleases == 2,
           "a destroyed mesh is released once and its handle goes stale");

  testSection("GpuDevice: programs, textures and buffers");
  std::string error;
  ShaderSources sources;
  testTrue(counters,
           !device.createShader(sources, &error).isValid() &&
             error == "no vertex source",
           "a program the API cannot build is refused with its error");
  sources.vertexSource = "void main() {}";
  const ShaderHandle shader = device.createShader(sources, &error);
  testTrue(counters, device.isShaderValid(shader), "a program is created");
  testTrue(counters,
           device.destroyShader(shader) && device.programReleases == 1 &&
             !device.isShaderValid(shader),
           "a destroyed program is released");
  const std::vector<unsigned char> pixels(64, 255);
  testTrue(counters,
           !device.createTexture(nullptr, 4, 4, 4, TextureOptions{}).isValid(),
           "a texture without pixels is refused");
  const TextureHandle texture =
    device.createTexture(pixels.data(), 4, 2, 1, TextureOptions{});
  const TextureInfo info = device.textureInfo(texture);
  testTrue(counters,
           info.width == 4 && info.height == 2 && info.channels == 1,
           "texture info reports the created size and channels");
  testTrue(counters,
           !device.replaceCubemap(texture, {}, 4, 4, 4),
           "a 2D texture cannot be replaced by a cubemap");
  const std::array<const unsigned char*, 6> faces = {
    pixels.data(), pixels.data(), pixels.data(),
    pixels.data(), pixels.data(), nullptr
  };
  testTrue(counters,
           !device.createCubemap(faces, 2, 2, 4).isValid(),
           "a cubemap with a missing face is refused");
  testTrue(counters,
           !device.createBuffer(BufferUsage::Instance, 0).isValid(),
           "an empty buffer is refused");
  const BufferHandle buffer = device.createBuffer(BufferUsage::Uniform, 64);
  testTrue(counters,
           device.buffer(buffer) != nullptr &&
             device.buffer(buffer)->usage == BufferUsage::Uniform &&
             device.buffer(buffer)->capacity == 64,
           "a buffer keeps its usage and capacity");

  testSection("GpuDevice: framebuffers own their attachment textures");
  FramebufferDesc desc;
  desc.width = 32;
  desc.height = 16;
  desc.colorAttachments.resize(2);
  desc.depthStencilFormat = TextureFormat::Depth24;
  FramebufferAttachments attachments;
  const FramebufferHandle framebuffer =
    device.createFramebuffer(desc, &attachments);
  testTrue(counters,
           device.isFramebufferValid(framebuffer) &&
             attachments.colorTextures.size() == 2 &&
             attachments.depthStencilTexture.isValid(),
           "a framebuffer registers two colour and one depth texture");
  const TextureInfo target = device.textureInfo(attachments.colorTextures[1]);
  testTrue(counters,
           target.width == 32 && target.height == 16,
           "attachment textures have the framebuffer's size");
  FramebufferDesc tooMany = desc;
  tooMany.colorAttachments.resize(9);
  testTrue(counters,
           !device.createFramebuffer(tooMany, nullptr).isValid(),
           "more colour attachments than the API allows are refused");
  const int texturesBefore = device.textureReleases;
  testTrue(counters,
           device.destroyFramebuffer(framebuffer) &&
             device.releasedTargets.size() == 1 &&
             device.releasedTargets[0] == framebuffer &&
             device.textureReleases == texturesBefore + 3 &&
             !device.isTextureValid(attachments.depthStencilTexture),
           "destroying a framebuffer stops drawing to it and frees its "
           "textures");

  testSection("GpuDevice: shutdown releases everything");
  device.shutdown();
  testTrue(counters,
           !device.isTextureValid(texture) && !device.isBufferValid(buffer) &&
             device.bufferReleases == 1,
           "every resource is released and every handle goes stale");
  return counters.failures;
}

static int
testTokenValidation()
{
  TestCounters counters;
  testSection("GpuDevice: draws reach the API only when valid");
  RecordingDevice device;
  const std::vector<float> vertices(24, 1.0f);
  const unsigned int indices[3] = { 0, 1, 2 };
  const MeshHandle mesh = device.createMesh(vertices.data(),
                                            96,
                                            indices,
                                            sizeof(indices),
                                            MeshVertexLayout::Pos3Color3Uv2,
                                            false);
  const MeshHandle plain = device.createMesh(
    vertices.data(), 96, nullptr, 0, MeshVertexLayout::Pos3Color3Uv2, false);
  ShaderSources sources;
  sources.vertexSource = "void main() {}";
  std::string error;
  const ShaderHandle shader = device.createShader(sources, &error);

  device.resetFrameError();
  submit(device, { drawArrays(3, 0) });
  testTrue(counters,
           device.draws.empty() && device.frameError() ==
                                     "Draw: missing valid shader or mesh; "
                                     "ignored",
           "a draw without a program or mesh is a frame error");
  testEqSize(counters, device.frameStats().submits, 1, "submits are counted");

  device.resetFrameError();
  submit(
    device,
    { viewport(0, 0), bindShader(shader), bindMesh(mesh), drawArrays(3, 0) });
  testTrue(counters,
           device.draws.empty() && device.frameError().empty(),
           "an empty viewport skips the draw silently");

  submit(device,
         { viewport(64, 64),
           bindShader(shader),
           bindMesh(mesh),
           drawArrays(3, 0),
           drawArrays(4, 0),
           drawIndexed(3, 0),
           drawIndexed(2, 2) });
  testEqSize(counters, device.draws.size(), 2, "two draws are in range");
  if (device.draws.size() == 2) {
    testTrue(counters,
             device.draws[0].count == 3 && device.draws[0].instances == 1 &&
               device.draws[1].count == 3 && device.draws[1].kind == 1,
             "the API receives the counts and the draw kind");
  }

  device.draws.clear();
  submit(device, { bindShader(shader), bindMesh(plain), drawIndexed(1, 0) });
  testTrue(counters,
           device.draws.empty(),
           "an indexed draw of a mesh without indices is skipped");

  device.resetFrameError();
  submit(device, { drawArrays(3, 0) });
  testTrue(counters,
           device.draws.empty() && !device.frameError().empty(),
           "each submission forgets the bound program and mesh");

  testSection("GpuDevice: updates are range-checked");
  const unsigned char bytes[16] = {};
  RenderCommand update = token(CommandType::UpdateBuffer);
  update.updateBuffer = CmdUpdateBuffer{ mesh, 90, 16, bytes };
  device.resetFrameError();
  submit(device, { update });
  testEqStr(counters,
            device.frameError(),
            "UpdateBuffer: range exceeds enrolled vertex capacity",
            "a vertex update past the capacity is refused");
  RenderCommand indexUpdate = token(CommandType::UpdateIndexBuffer);
  indexUpdate.updateIndexBuffer = CmdUpdateBuffer{ plain, 0, 4, bytes };
  device.resetFrameError();
  submit(device, { indexUpdate });
  testEqStr(counters,
            device.frameError(),
            "UpdateIndexBuffer: range exceeds index capacity",
            "an index update of a mesh without indices is refused");
  update.updateBuffer = CmdUpdateBuffer{ mesh, 80, 16, bytes };
  device.resetFrameError();
  submit(device, { update });
  testTrue(counters,
           device.frameError().empty(),
           "a vertex update inside the capacity is accepted");

  const BufferHandle instances =
    device.createBuffer(BufferUsage::Instance, 144 * 2);
  RenderCommand write = token(CommandType::WriteBuffer);
  write.writeBuffer = CmdWriteBuffer{ instances, 280, 16, bytes };
  device.resetFrameError();
  submit(device, { write });
  testTrue(counters,
           !device.frameError().empty(),
           "a buffer write past the capacity is refused");

  const std::vector<unsigned char> pixels(64, 255);
  const TextureHandle texture =
    device.createTexture(pixels.data(), 4, 4, 4, TextureOptions{});
  RenderCommand textureUpdate = token(CommandType::UpdateTexture);
  textureUpdate.updateTexture.handle = texture;
  textureUpdate.updateTexture.x = 2;
  textureUpdate.updateTexture.y = 2;
  textureUpdate.updateTexture.width = 2;
  textureUpdate.updateTexture.height = 2;
  textureUpdate.updateTexture.channels = 0;
  textureUpdate.updateTexture.srcRowStride = 0;
  textureUpdate.updateTexture.data = pixels.data();
  device.resetFrameStats();
  device.resetFrameError();
  submit(device, { textureUpdate });
  testTrue(counters,
           device.textureUploads == 1 && device.frameStats().uploadBytes == 16,
           "a texture update in bounds uploads and counts its bytes");
  textureUpdate.updateTexture.x = 3;
  submit(device, { textureUpdate });
  testTrue(counters,
           device.textureUploads == 1 &&
             device.frameError() ==
               "UpdateTexture: invalid rectangle, channels, or stride",
           "a texture update out of bounds is refused");

  testSection("GpuDevice: instanced draws need an instance stream");
  RenderCommand instanced = token(CommandType::DrawIndexedInstanced);
  instanced.drawIndexedInstanced = CmdDrawIndexedInstanced{ 3, 0, 2 };
  device.draws.clear();
  device.resetFrameError();
  submit(device,
         { viewport(64, 64), bindShader(shader), bindMesh(mesh), instanced });
  testTrue(counters,
           device.draws.empty() && !device.frameError().empty(),
           "an instanced draw without a stream is refused");
  RenderCommand stream = token(CommandType::SetInstanceStream);
  stream.instanceStream =
    CmdInstanceStream{ instances, 0, InstanceLayout::LitModelTint };
  device.resetFrameError();
  submit(device,
         { viewport(64, 64),
           bindShader(shader),
           bindMesh(mesh),
           stream,
           instanced });
  testTrue(counters,
           device.draws.size() == 1 && device.draws[0].instances == 2 &&
             device.frameError().empty(),
           "an instanced draw within the stream's capacity reaches the API");
  instanced.drawIndexedInstanced = CmdDrawIndexedInstanced{ 3, 0, 3 };
  submit(device,
         { viewport(64, 64),
           bindShader(shader),
           bindMesh(mesh),
           stream,
           instanced });
  testTrue(counters,
           device.draws.size() == 1 && !device.frameError().empty(),
           "more instances than the stream holds are refused");

  RenderCommand list = token(CommandType::ExecuteList);
  list.executeList.list = nullptr;
  device.resetFrameError();
  submit(device, { list, token(CommandType::ClearScreen) });
  testTrue(counters,
           device.frameError() ==
               "ExecuteList: missing or failed recorded list" &&
             device.clears == 1,
           "a missing recorded list is a frame error; later tokens run");
  return counters.failures;
}

void
registerGpuDeviceTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.GpuDevice.Resources", []() { return testResources(); });
  registry.add("Illumo.GpuDevice.TokenValidation",
               []() { return testTokenValidation(); });
}
