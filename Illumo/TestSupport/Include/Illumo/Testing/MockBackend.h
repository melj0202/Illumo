#pragma once
#include <Illumo/Rendering/CommandQueue.h>
#include <Illumo/Rendering/IBackend.h>
#include <Illumo/Rendering/ResourceHandlePool.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Headless IBackend for automated tests.
// Records enroll/create calls and snapshots the command queue on Submit.
// Does not touch OpenGL or any GPU API.
class MockBackend : public IBackend
{
public:
  struct CreateRecord
  {
    enum class Kind
    {
      Mesh,
      ShaderPaths,
      ShaderSources,
      TextureData,
      ReplaceMesh,
      ReplaceShader,
      ReplaceTexture,
      DestroyMesh,
      DestroyShader,
      DestroyTexture
    };
    Kind kind = Kind::Mesh;
    uint32_t slot = 0;
    uint32_t generation = 0;
    size_t vertexSize = 0;
    size_t indexSize = 0;
    int width = 0;
    int height = 0;
    int channels = 0;
    TextureFilter filter = TextureFilter::Nearest;
    MeshVertexLayout layout = MeshVertexLayout::Pos3Color3Uv2;
    bool dynamic = false;
    std::string pathOrNote;
  };

private:
  CommandQueue commandQueue;
  std::vector<RenderCommand> lastSubmitted;
  // Survives empty EndFrame re-submits (production EndFrame submits after
  // Clear).
  std::vector<RenderCommand> lastNonEmptySubmitted;
  std::vector<std::vector<RenderCommand>> submittedFrames;
  std::vector<CreateRecord> creates;
  int beginFrameCount = 0;
  int endFrameCount = 0;
  int submitCount = 0;
  int fps = 0;
  bool initialized = false;
  bool shutDown = false;
  size_t rejectedStaleCommands = 0;
  bool rejectNextShaderReplacement = false;
  bool rejectNextTextureReplacement = false;
  ResourceHandlePool<MeshHandle> meshHandles;
  ResourceHandlePool<ShaderHandle> shaderHandles;
  ResourceHandlePool<TextureHandle> textureHandles;
  ResourceHandlePool<FramebufferHandle> framebufferHandles;
  std::unordered_map<uint32_t, uint32_t> liveMeshes;
  std::unordered_map<uint32_t, uint32_t> liveShaders;
  std::unordered_map<uint32_t, uint32_t> liveTextures;
  std::unordered_map<uint32_t, uint32_t> liveFramebuffers;
  std::unordered_map<uint32_t, TextureInfo> textureInfos;

  bool isCommandResourceValid(const RenderCommand& command) const
  {
    switch (command.commandType) {
      case CommandType::SetFramebuffer:
        return !command.bindFramebuffer.handle.isValid() ||
               IsFramebufferValid(command.bindFramebuffer.handle);
      case CommandType::SetMesh:
        return IsMeshValid(command.bindMesh.handle);
      case CommandType::SetShader:
        return IsShaderValid(command.bindShader.handle);
      case CommandType::SetTexture:
        return IsTextureValid(command.bindTexture.handle);
      case CommandType::UpdateBuffer:
        return IsMeshValid(command.updateBuffer.handle);
      case CommandType::UpdateTexture:
        return IsTextureValid(command.updateTexture.handle);
      default:
        return true;
    }
  }

public:
  MockBackend() = default;
  ~MockBackend() override = default;

  bool Initialize() override
  {
    initialized = true;
    shutDown = false;
    return true;
  }

  void Shutdown() override
  {
    commandQueue.Reset();
    lastSubmitted.clear();
    creates.clear();
    liveMeshes.clear();
    liveShaders.clear();
    liveTextures.clear();
    textureInfos.clear();
    meshHandles.clear();
    shaderHandles.clear();
    textureHandles.clear();
    shutDown = true;
  }

  void BeginFrame() override { ++beginFrameCount; }

  void EndFrame() override { ++endFrameCount; }

  void SubmitCommandQueue() override
  {
    lastSubmitted.clear();
    const size_t n = commandQueue.GetCommandCount();
    for (size_t i = 0; i < n; ++i) {
      const RenderCommand& command = commandQueue.GetCommand(i);
      if (isCommandResourceValid(command)) {
        lastSubmitted.push_back(command);
      } else {
        rejectedStaleCommands++;
      }
    }
    if (!lastSubmitted.empty()) {
      lastNonEmptySubmitted = lastSubmitted;
    }
    submittedFrames.push_back(lastSubmitted);
    ++submitCount;
  }

  void PushToCommandQueue(RenderCommand command) override
  {
    commandQueue.Submit(command);
  }

  void ClearCommandQueue() override { commandQueue.Reset(); }

  int getFPS() const override { return fps; }

  void setFPS(int value) { fps = value; }

  MeshHandle CreateMesh(const void* vertices,
                        size_t vertexSize,
                        const void* indices,
                        size_t indexSize) override
  {
    return CreateMesh(vertices,
                      vertexSize,
                      indices,
                      indexSize,
                      MeshVertexLayout::Pos3Color3Uv2,
                      false);
  }

  MeshHandle CreateMesh(const void* vertices,
                        size_t vertexSize,
                        const void* indices,
                        size_t indexSize,
                        MeshVertexLayout layout,
                        bool dynamic) override
  {
    (void)vertices;
    (void)indices;
    MeshHandle handle = meshHandles.allocate();
    CreateRecord rec;
    rec.kind = CreateRecord::Kind::Mesh;
    rec.slot = handle.slot;
    rec.generation = handle.generation;
    rec.vertexSize = vertexSize;
    rec.indexSize = indexSize;
    rec.layout = layout;
    rec.dynamic = dynamic;
    creates.push_back(rec);
    liveMeshes[handle.slot] = handle.generation;
    return handle;
  }

  bool ReplaceMesh(MeshHandle handle,
                   const void* vertices,
                   size_t vertexSize,
                   const void* indices,
                   size_t indexSize,
                   MeshVertexLayout layout,
                   bool dynamic) override
  {
    (void)vertices;
    (void)indices;
    if (!IsMeshValid(handle)) {
      return false;
    }
    CreateRecord rec;
    rec.kind = CreateRecord::Kind::ReplaceMesh;
    rec.slot = handle.slot;
    rec.generation = handle.generation;
    rec.vertexSize = vertexSize;
    rec.indexSize = indexSize;
    rec.layout = layout;
    rec.dynamic = dynamic;
    creates.push_back(rec);
    return true;
  }

  bool DestroyMesh(MeshHandle handle) override
  {
    if (!IsMeshValid(handle)) {
      return false;
    }
    CreateRecord rec;
    rec.kind = CreateRecord::Kind::DestroyMesh;
    rec.slot = handle.slot;
    rec.generation = handle.generation;
    creates.push_back(rec);
    liveMeshes.erase(handle.slot);
    return meshHandles.release(handle);
  }

  bool IsMeshValid(MeshHandle handle) const override
  {
    std::unordered_map<uint32_t, uint32_t>::const_iterator it =
      liveMeshes.find(handle.slot);
    return meshHandles.isCurrent(handle) && it != liveMeshes.end() &&
           it->second == handle.generation;
  }

  ShaderHandle CreateShaderProgram(const ShaderPaths& paths) override
  {
    ShaderHandle handle = shaderHandles.allocate();
    CreateRecord rec;
    rec.kind = CreateRecord::Kind::ShaderPaths;
    rec.slot = handle.slot;
    rec.generation = handle.generation;
    rec.pathOrNote = paths.vertexPath + "|" + paths.fragmentPath;
    creates.push_back(rec);
    liveShaders[handle.slot] = handle.generation;
    return handle;
  }

  ShaderHandle CreateShaderProgram(const ShaderSources& sources) override
  {
    (void)sources;
    ShaderHandle handle = shaderHandles.allocate();
    CreateRecord rec;
    rec.kind = CreateRecord::Kind::ShaderSources;
    rec.slot = handle.slot;
    rec.generation = handle.generation;
    rec.pathOrNote = "inline_sources";
    creates.push_back(rec);
    liveShaders[handle.slot] = handle.generation;
    return handle;
  }

  bool ReplaceShaderProgram(ShaderHandle handle,
                            const ShaderSources& sources) override
  {
    (void)sources;
    if (!IsShaderValid(handle) || rejectNextShaderReplacement) {
      rejectNextShaderReplacement = false;
      return false;
    }
    CreateRecord rec;
    rec.kind = CreateRecord::Kind::ReplaceShader;
    rec.slot = handle.slot;
    rec.generation = handle.generation;
    creates.push_back(rec);
    return true;
  }

  bool DestroyShaderProgram(ShaderHandle handle) override
  {
    if (!IsShaderValid(handle)) {
      return false;
    }
    CreateRecord rec;
    rec.kind = CreateRecord::Kind::DestroyShader;
    rec.slot = handle.slot;
    rec.generation = handle.generation;
    creates.push_back(rec);
    liveShaders.erase(handle.slot);
    return shaderHandles.release(handle);
  }

  bool IsShaderValid(ShaderHandle handle) const override
  {
    std::unordered_map<uint32_t, uint32_t>::const_iterator it =
      liveShaders.find(handle.slot);
    return shaderHandles.isCurrent(handle) && it != liveShaders.end() &&
           it->second == handle.generation;
  }

  TextureHandle CreateTexture(const unsigned char* data,
                              const int width,
                              const int height) override
  {
    TextureOptions options;
    return CreateTexture(data, width, height, 4, options);
  }

  TextureHandle CreateTexture(const unsigned char* data,
                              const int width,
                              const int height,
                              int channels,
                              const TextureOptions& options) override
  {
    (void)data;
    TextureHandle handle = textureHandles.allocate();
    CreateRecord rec;
    rec.kind = CreateRecord::Kind::TextureData;
    rec.slot = handle.slot;
    rec.generation = handle.generation;
    rec.width = width;
    rec.height = height;
    rec.channels = channels;
    rec.filter = options.filter;
    creates.push_back(rec);
    liveTextures[handle.slot] = handle.generation;
    TextureInfo info;
    info.width = width;
    info.height = height;
    info.channels = channels;
    textureInfos[handle.slot] = info;
    return handle;
  }

  bool ReplaceTexture(TextureHandle handle,
                      const unsigned char* data,
                      int width,
                      int height,
                      int channels,
                      const TextureOptions& options) override
  {
    (void)data;
    if (!IsTextureValid(handle) || rejectNextTextureReplacement) {
      rejectNextTextureReplacement = false;
      return false;
    }
    CreateRecord rec;
    rec.kind = CreateRecord::Kind::ReplaceTexture;
    rec.slot = handle.slot;
    rec.generation = handle.generation;
    rec.width = width;
    rec.height = height;
    rec.channels = channels;
    rec.filter = options.filter;
    creates.push_back(rec);
    TextureInfo info;
    info.width = width;
    info.height = height;
    info.channels = channels;
    textureInfos[handle.slot] = info;
    return true;
  }

  bool DestroyTexture(TextureHandle handle) override
  {
    if (!IsTextureValid(handle)) {
      return false;
    }
    CreateRecord rec;
    rec.kind = CreateRecord::Kind::DestroyTexture;
    rec.slot = handle.slot;
    rec.generation = handle.generation;
    creates.push_back(rec);
    liveTextures.erase(handle.slot);
    textureInfos.erase(handle.slot);
    return textureHandles.release(handle);
  }

  bool IsTextureValid(TextureHandle handle) const override
  {
    std::unordered_map<uint32_t, uint32_t>::const_iterator it =
      liveTextures.find(handle.slot);
    return textureHandles.isCurrent(handle) && it != liveTextures.end() &&
           it->second == handle.generation;
  }

  TextureInfo GetTextureInfo(TextureHandle handle) const override
  {
    std::unordered_map<uint32_t, TextureInfo>::const_iterator it =
      textureInfos.find(handle.slot);
    if (!IsTextureValid(handle) || it == textureInfos.end()) {
      return TextureInfo{};
    }
    return it->second;
  }

  FramebufferHandle CreateFramebuffer(
    const FramebufferDesc& desc,
    FramebufferAttachments* outAttachments = nullptr) override
  {
    std::vector<TextureHandle> colHandles;
    colHandles.reserve(desc.colorAttachments.size());
    for (size_t i = 0; i < desc.colorAttachments.size(); ++i) {
      const FramebufferAttachmentDesc& att = desc.colorAttachments[i];
      TextureOptions opt;
      opt.filter = att.filter;
      opt.wrapX = att.wrap;
      opt.wrapY = att.wrap;
      int ch = 4;
      if (att.format == TextureFormat::RGB8) {
        ch = 3;
      } else if (att.format == TextureFormat::R8 ||
                 att.format == TextureFormat::R16F) {
        ch = 1;
      } else if (att.format == TextureFormat::RG16F) {
        ch = 2;
      }

      TextureHandle tex =
        CreateTexture(nullptr, desc.width, desc.height, ch, opt);
      colHandles.push_back(tex);
    }

    TextureHandle depthHandle{};
    if (desc.depthStencilFormat != TextureFormat::None) {
      TextureOptions opt;
      opt.filter = desc.depthFilter;
      opt.wrapX = TextureWrap::ClampToEdge;
      opt.wrapY = TextureWrap::ClampToEdge;
      depthHandle = CreateTexture(nullptr, desc.width, desc.height, 1, opt);
    }

    if (outAttachments) {
      outAttachments->colorTextures = colHandles;
      outAttachments->depthStencilTexture = depthHandle;
    }

    FramebufferHandle fb = framebufferHandles.allocate();
    liveFramebuffers[fb.slot] = fb.generation;
    return fb;
  }

  FramebufferHandle CreateDepthFramebuffer(
    int width,
    int height,
    TextureHandle* outDepthTexture) override
  {
    FramebufferDesc desc;
    desc.width = width;
    desc.height = height;
    desc.depthStencilFormat = TextureFormat::Depth24;
    desc.depthFilter = TextureFilter::Nearest;
    FramebufferAttachments atts;
    FramebufferHandle fb = CreateFramebuffer(desc, &atts);
    if (outDepthTexture) {
      *outDepthTexture = atts.depthStencilTexture;
    }
    return fb;
  }

  bool DestroyFramebuffer(FramebufferHandle handle) override
  {
    std::unordered_map<uint32_t, uint32_t>::iterator it =
      liveFramebuffers.find(handle.slot);
    if (it == liveFramebuffers.end() || it->second != handle.generation) {
      return false;
    }
    liveFramebuffers.erase(it);
    return framebufferHandles.release(handle);
  }

  bool IsFramebufferValid(FramebufferHandle handle) const override
  {
    std::unordered_map<uint32_t, uint32_t>::const_iterator it =
      liveFramebuffers.find(handle.slot);
    return framebufferHandles.isCurrent(handle) &&
           it != liveFramebuffers.end() && it->second == handle.generation;
  }

  bool wasInitialized() const { return initialized; }
  bool wasShutdown() const { return shutDown; }
  int getBeginFrameCount() const { return beginFrameCount; }
  int getEndFrameCount() const { return endFrameCount; }
  int getSubmitCount() const { return submitCount; }
  size_t getRejectedStaleCommandCount() const { return rejectedStaleCommands; }

  void setRejectNextShaderReplacement(bool reject)
  {
    rejectNextShaderReplacement = reject;
  }

  void setRejectNextTextureReplacement(bool reject)
  {
    rejectNextTextureReplacement = reject;
  }

  size_t getPendingCommandCount() const
  {
    return commandQueue.GetCommandCount();
  }

  size_t getLastSubmittedCount() const { return lastSubmitted.size(); }

  const RenderCommand& getLastSubmitted(size_t index) const
  {
    return lastSubmitted[index];
  }

  CommandType getLastSubmittedType(size_t index) const
  {
    return lastSubmitted[index].commandType;
  }

  size_t getLastNonEmptySubmittedCount() const
  {
    return lastNonEmptySubmitted.size();
  }

  const RenderCommand& getLastNonEmptySubmitted(size_t index) const
  {
    return lastNonEmptySubmitted[index];
  }

  CommandType getLastNonEmptySubmittedType(size_t index) const
  {
    return lastNonEmptySubmitted[index].commandType;
  }

  size_t getSubmittedFrameCount() const { return submittedFrames.size(); }

  const std::vector<RenderCommand>& getSubmittedFrame(size_t frameIndex) const
  {
    return submittedFrames[frameIndex];
  }

  size_t countNonEmptyOfType(CommandType type) const
  {
    size_t n = 0;
    for (size_t i = 0; i < lastNonEmptySubmitted.size(); ++i) {
      if (lastNonEmptySubmitted[i].commandType == type) {
        ++n;
      }
    }
    return n;
  }

  bool nonEmptyStartsWith(const CommandType* types, size_t typeCount) const
  {
    if (typeCount > lastNonEmptySubmitted.size()) {
      return false;
    }
    for (size_t i = 0; i < typeCount; ++i) {
      if (lastNonEmptySubmitted[i].commandType != types[i]) {
        return false;
      }
    }
    return true;
  }

  size_t getCreateCount() const { return creates.size(); }

  const CreateRecord& getCreate(size_t index) const { return creates[index]; }

  size_t countSubmittedOfType(CommandType type) const
  {
    size_t n = 0;
    for (size_t i = 0; i < lastSubmitted.size(); ++i) {
      if (lastSubmitted[i].commandType == type) {
        ++n;
      }
    }
    return n;
  }

  bool submittedStartsWith(const CommandType* types, size_t typeCount) const
  {
    if (typeCount > lastSubmitted.size()) {
      return false;
    }
    for (size_t i = 0; i < typeCount; ++i) {
      if (lastSubmitted[i].commandType != types[i]) {
        return false;
      }
    }
    return true;
  }

  void resetCounters()
  {
    beginFrameCount = 0;
    endFrameCount = 0;
    submitCount = 0;
    lastSubmitted.clear();
    lastNonEmptySubmitted.clear();
    submittedFrames.clear();
    commandQueue.Reset();
    creates.clear();
    rejectedStaleCommands = 0;
  }
};
