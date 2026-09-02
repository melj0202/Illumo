#pragma once
#include "GL/glew.h"
#ifndef GL_GPU_MEM_INFO_TOTAL_AVAILABLE_MEM_NVX
#define GL_GPU_MEM_INFO_TOTAL_AVAILABLE_MEM_NVX 0x9048
#endif
#include "GLMesh.h"
#include "GLShaderProgram.h"
#include "GLTexture.h"
#include "Rendering/HWInfo.h"
#include <Illumo/Rendering/CommandQueue.h>
#include <Illumo/Rendering/PipelineState.h>
#include <Illumo/Rendering/ResourceHandle.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct GLMeshResourceEntry
{
  uint32_t generation = 0;
  std::unique_ptr<GLMesh> resource;
};

struct GLShaderResourceEntry
{
  uint32_t generation = 0;
  std::unique_ptr<GLShaderProgram> resource;
};

struct GLTextureResourceEntry
{
  uint32_t generation = 0;
  std::unique_ptr<GLTexture> resource;
};

struct GLFramebufferResourceEntry
{
  uint32_t generation = 0;
  GLuint fboId = 0;
  std::vector<TextureHandle> colorTextures;
  TextureHandle depthTexture{};
  int width = 0;
  int height = 0;
};

// Registries owned by GLBackend; typed handles resolve by slot and generation.
struct GLResourceTables
{
  const std::unordered_map<uint32_t, GLMeshResourceEntry>* meshes = nullptr;
  const std::unordered_map<uint32_t, GLShaderResourceEntry>* programs = nullptr;
  const std::unordered_map<uint32_t, GLTextureResourceEntry>* textures =
    nullptr;
  const std::unordered_map<uint32_t, GLFramebufferResourceEntry>* framebuffers =
    nullptr;
};

class GLDevice
{
private:
  PipelineState _currentGLState;
  GLuint _activeProgram = 0;

  // Bind-state tracker (P4): skip redundant GL binds within a submit.
  GLuint _boundProgram = 0;
  GLuint _boundVao = 0;
  GLuint _boundTexture[8] = {};
  GLuint _boundFbo = 0;
  int _viewportX = -1;
  int _viewportY = -1;
  int _viewportW = -1;
  int _viewportH = -1;

  // Cache: key is "progId:name"
  std::unordered_map<std::string, GLint> _uniformLocationCache;

  GLenum mapBlendFactor(BlendFactor factor)
  {
    switch (factor) {
      case BlendFactor::Zero:
        return GL_ZERO;
      case BlendFactor::One:
        return GL_ONE;
      case BlendFactor::SrcAlpha:
        return GL_SRC_ALPHA;
      case BlendFactor::OneMinusSrcAlpha:
        return GL_ONE_MINUS_SRC_ALPHA;
      case BlendFactor::SrcColor:
        return GL_SRC_COLOR;
      case BlendFactor::OneMinusSrcColor:
        return GL_ONE_MINUS_SRC_COLOR;
      default:
        return GL_ONE;
    }
  }

  GLenum mapCullMode(CullMode mode)
  {
    switch (mode) {
      case CullMode::Front:
        return GL_FRONT;
      case CullMode::Back:
        return GL_BACK;
      case CullMode::FrontAndBack:
        return GL_FRONT_AND_BACK;
      default:
        return GL_BACK;
    }
  }

  GLenum mapWindingOrder(WindingOrder order)
  {
    switch (order) {
      case WindingOrder::Clockwise:
        return GL_CW;
      case WindingOrder::CounterClockwise:
        return GL_CCW;
      default:
        return GL_CCW;
    }
  }

  GLenum mapPrimitives(Primitives p)
  {
    switch (p) {
      case Primitives::Points:
        return GL_POINTS;
      case Primitives::Lines:
        return GL_LINES;
      case Primitives::Triangles:
        return GL_TRIANGLES;
      default:
        return GL_TRIANGLES;
    }
  }

  GLint getUniformLocation(const char* name)
  {
    if (_activeProgram == 0 || name == nullptr) {
      return -1;
    }
    std::string key = std::to_string(_activeProgram);
    key.push_back(':');
    key.append(name);
    std::unordered_map<std::string, GLint>::iterator it =
      _uniformLocationCache.find(key);
    if (it != _uniformLocationCache.end()) {
      return it->second;
    }
    GLint loc = glGetUniformLocation(_activeProgram, name);
    _uniformLocationCache[key] = loc;
    return loc;
  }

  GLMesh* resolveMesh(const GLResourceTables& tables, MeshHandle handle) const
  {
    if (!tables.meshes) {
      return nullptr;
    }
    std::unordered_map<uint32_t, GLMeshResourceEntry>::const_iterator it =
      tables.meshes->find(handle.slot);
    if (it == tables.meshes->end() ||
        it->second.generation != handle.generation) {
      return nullptr;
    }
    return it->second.resource.get();
  }

  GLShaderProgram* resolveProgram(const GLResourceTables& tables,
                                  ShaderHandle handle) const
  {
    if (!tables.programs) {
      return nullptr;
    }
    std::unordered_map<uint32_t, GLShaderResourceEntry>::const_iterator it =
      tables.programs->find(handle.slot);
    if (it == tables.programs->end() ||
        it->second.generation != handle.generation) {
      return nullptr;
    }
    return it->second.resource.get();
  }

  GLTexture* resolveTexture(const GLResourceTables& tables,
                            TextureHandle handle) const
  {
    if (!tables.textures) {
      return nullptr;
    }
    std::unordered_map<uint32_t, GLTextureResourceEntry>::const_iterator it =
      tables.textures->find(handle.slot);
    if (it == tables.textures->end() ||
        it->second.generation != handle.generation) {
      return nullptr;
    }
    return it->second.resource.get();
  }

  const GLFramebufferResourceEntry* resolveFramebuffer(
    const GLResourceTables& tables,
    FramebufferHandle handle) const
  {
    if (!tables.framebuffers) {
      return nullptr;
    }
    std::unordered_map<uint32_t, GLFramebufferResourceEntry>::const_iterator
      it = tables.framebuffers->find(handle.slot);
    if (it == tables.framebuffers->end() ||
        it->second.generation != handle.generation) {
      return nullptr;
    }
    return &it->second;
  }

public:
  void ApplyPipelineState(const PipelineState& pipelineState);
  void ExecuteCommandQueue(CommandQueue& commandQueue,
                           const GLResourceTables& tables);
  HWInfo GetHWInfo();
};
