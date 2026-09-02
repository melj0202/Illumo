#include "GLDevice.h"
#include <Illumo/Services/Logger.h>

void
GLDevice::ApplyPipelineState(const PipelineState& pipelineState)
{
  if (pipelineState.depthTestEnabled != _currentGLState.depthTestEnabled) {
    if (pipelineState.depthTestEnabled) {
      glEnable(GL_DEPTH_TEST);
    } else {
      glDisable(GL_DEPTH_TEST);
    }
    _currentGLState.depthTestEnabled = pipelineState.depthTestEnabled;
  }

  // OpenGL default blend is ONE/ZERO. Tracked PipelineState defaults are
  // SrcAlpha/OneMinusSrcAlpha, so we must set glBlendFunc when enabling
  // blend — not only when factors differ from our CPU-side cache.
  if (pipelineState.blendEnabled != _currentGLState.blendEnabled) {
    if (pipelineState.blendEnabled) {
      glEnable(GL_BLEND);
      glBlendFunc(mapBlendFactor(pipelineState.blendSrc),
                  mapBlendFactor(pipelineState.blendDst));
      _currentGLState.blendSrc = pipelineState.blendSrc;
      _currentGLState.blendDst = pipelineState.blendDst;
    } else {
      glDisable(GL_BLEND);
    }
    _currentGLState.blendEnabled = pipelineState.blendEnabled;
  } else if (pipelineState.blendEnabled) {
    if (pipelineState.blendSrc != _currentGLState.blendSrc ||
        pipelineState.blendDst != _currentGLState.blendDst) {
      glBlendFunc(mapBlendFactor(pipelineState.blendSrc),
                  mapBlendFactor(pipelineState.blendDst));
      _currentGLState.blendSrc = pipelineState.blendSrc;
      _currentGLState.blendDst = pipelineState.blendDst;
    }
  }

  if (pipelineState.faceCullingEnabled != _currentGLState.faceCullingEnabled) {
    if (pipelineState.faceCullingEnabled) {
      glEnable(GL_CULL_FACE);
    } else {
      glDisable(GL_CULL_FACE);
    }
    _currentGLState.faceCullingEnabled = pipelineState.faceCullingEnabled;
  }
  if (pipelineState.faceCullingEnabled) {
    if (pipelineState.cullFace != _currentGLState.cullFace) {
      glCullFace(mapCullMode(pipelineState.cullFace));
      _currentGLState.cullFace = pipelineState.cullFace;
    }
    if (pipelineState.frontFace != _currentGLState.frontFace) {
      glFrontFace(mapWindingOrder(pipelineState.frontFace));
      _currentGLState.frontFace = pipelineState.frontFace;
    }
  }

  if (pipelineState.wireframe != _currentGLState.wireframe) {
    glPolygonMode(GL_FRONT_AND_BACK,
                  pipelineState.wireframe ? GL_LINE : GL_FILL);
    _currentGLState.wireframe = pipelineState.wireframe;
  }

  _currentGLState.primitives = pipelineState.primitives;
}

void
GLDevice::ExecuteCommandQueue(CommandQueue& commandQueue,
                              const GLResourceTables& tables)
{
  // Fresh submit: do not assume previous frame left valid binds (other code may
  // touch GL). Still skip duplicates *within* this queue.
  _boundProgram = 0;
  _boundVao = 0;
  for (int s = 0; s < 8; ++s) {
    _boundTexture[s] = 0;
  }
  // Not 0: first SetFramebuffer({}) must still call glBindFramebuffer(0).
  _boundFbo = static_cast<GLuint>(-1);
  _viewportX = -1;
  _viewportY = -1;
  _viewportW = -1;
  _viewportH = -1;
  _activeProgram = 0;

  for (size_t i = 0; i < commandQueue.GetCommandCount(); ++i) {
    RenderCommand& cmd = commandQueue.GetCommand(i);

    switch (cmd.commandType) {
      case CommandType::SetPipelineState:
        ApplyPipelineState(cmd.pipelineState);
        break;

      case CommandType::SetViewport:
        if (cmd.viewport.x != _viewportX || cmd.viewport.y != _viewportY ||
            cmd.viewport.width != _viewportW ||
            cmd.viewport.height != _viewportH) {
          glViewport(cmd.viewport.x,
                     cmd.viewport.y,
                     cmd.viewport.width,
                     cmd.viewport.height);
          _viewportX = cmd.viewport.x;
          _viewportY = cmd.viewport.y;
          _viewportW = cmd.viewport.width;
          _viewportH = cmd.viewport.height;
        }
        break;

      case CommandType::SetScissorState:
        if (cmd.scissor.enabled) {
          glEnable(GL_SCISSOR_TEST);
          glScissor(cmd.scissor.x,
                    cmd.scissor.y,
                    cmd.scissor.width,
                    cmd.scissor.height);
        } else {
          glDisable(GL_SCISSOR_TEST);
        }
        break;

      case CommandType::SetFramebuffer: {
        if (!cmd.bindFramebuffer.handle.isValid()) {
          if (_boundFbo != 0) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            _boundFbo = 0;
          }
          break;
        }
        const GLFramebufferResourceEntry* fb =
          resolveFramebuffer(tables, cmd.bindFramebuffer.handle);
        if (!fb) {
          Logger::LogWarning("SetFramebuffer: unknown framebuffer handle");
          glBindFramebuffer(GL_FRAMEBUFFER, 0);
          _boundFbo = 0;
          break;
        }
        if (fb->fboId != _boundFbo) {
          glBindFramebuffer(GL_FRAMEBUFFER, fb->fboId);
          _boundFbo = fb->fboId;
        }
        break;
      }

      case CommandType::SetShader: {
        GLShaderProgram* program =
          resolveProgram(tables, cmd.bindShader.handle);
        if (!program) {
          Logger::LogWarning("SetShader: unknown shader handle");
          glUseProgram(0);
          _boundProgram = 0;
          _activeProgram = 0;
          break;
        }
        GLuint id = static_cast<GLuint>(program->GetID());
        if (id != _boundProgram) {
          glUseProgram(id);
          _boundProgram = id;
        }
        _activeProgram = id;
        break;
      }

      case CommandType::SetMesh: {
        GLMesh* mesh = resolveMesh(tables, cmd.bindMesh.handle);
        if (!mesh) {
          Logger::LogWarning("SetMesh: unknown mesh handle");
          glBindVertexArray(0);
          _boundVao = 0;
          break;
        }
        const GLuint vao = mesh->getVAOID();
        if (vao != _boundVao) {
          mesh->Bind();
          _boundVao = vao;
        }
        break;
      }

      case CommandType::SetTexture: {
        GLTexture* texture = resolveTexture(tables, cmd.bindTexture.handle);
        if (!texture) {
          Logger::LogWarning("SetTexture: unknown texture handle");
          break;
        }
        const unsigned int slot = cmd.bindTexture.slot;
        const GLuint texId = static_cast<GLuint>(texture->getID());
        if (slot < 8 && _boundTexture[slot] == texId) {
          break;
        }
        texture->Bind(slot);
        if (slot < 8) {
          _boundTexture[slot] = texId;
        }
        break;
      }

      case CommandType::SetUniformInt: {
        GLint loc = getUniformLocation(cmd.uniformInt.name);
        if (loc >= 0) {
          glUniform1i(loc, cmd.uniformInt.value);
        }
        break;
      }

      case CommandType::SetUniformFloat: {
        GLint loc = getUniformLocation(cmd.uniformFloat.name);
        if (loc >= 0) {
          glUniform1f(loc, cmd.uniformFloat.value);
        }
        break;
      }

      case CommandType::SetUniformVec2: {
        GLint loc = getUniformLocation(cmd.uniformVec2.name);
        if (loc >= 0) {
          glUniform2f(loc, cmd.uniformVec2.x, cmd.uniformVec2.y);
        }
        break;
      }

      case CommandType::SetUniformVec3: {
        GLint loc = getUniformLocation(cmd.uniformVec3.name);
        if (loc >= 0) {
          glUniform3f(
            loc, cmd.uniformVec3.x, cmd.uniformVec3.y, cmd.uniformVec3.z);
        }
        break;
      }

      case CommandType::SetUniformVec4: {
        GLint loc = getUniformLocation(cmd.uniformVec4.name);
        if (loc >= 0) {
          glUniform4f(loc,
                      cmd.uniformVec4.x,
                      cmd.uniformVec4.y,
                      cmd.uniformVec4.z,
                      cmd.uniformVec4.w);
        }
        break;
      }

      case CommandType::SetUniformMat4: {
        GLint loc = getUniformLocation(cmd.uniformMat4.name);
        if (loc >= 0) {
          glUniformMatrix4fv(loc, 1, GL_FALSE, cmd.uniformMat4.m);
        }
        break;
      }

      case CommandType::UpdateTexture: {
        GLTexture* texture = resolveTexture(tables, cmd.updateTexture.handle);
        if (!texture || !cmd.updateTexture.data) {
          Logger::LogWarning("UpdateTexture: invalid handle or null data");
          break;
        }
        // PBO ping-pong + dirty-rect copy lives on GLTexture (P4).
        const int channels = (cmd.updateTexture.channels > 0)
                               ? cmd.updateTexture.channels
                               : texture->getChannels();
        texture->UpdateSubImage(cmd.updateTexture.x,
                                cmd.updateTexture.y,
                                cmd.updateTexture.width,
                                cmd.updateTexture.height,
                                channels,
                                cmd.updateTexture.data,
                                cmd.updateTexture.srcRowStride);
        // Texture bind may have changed inside UpdateSubImage.
        _boundTexture[0] = static_cast<GLuint>(texture->getID());
        break;
      }

      case CommandType::ClearScreen:
        if (cmd.clear.a > 0.0f || cmd.clear.r > 0.0f || cmd.clear.g > 0.0f ||
            cmd.clear.b > 0.0f ||
            (cmd.clear.r == 0.0f && cmd.clear.g == 0.0f &&
             cmd.clear.b == 0.0f)) {
          // Always apply clear color when ClearScreen is issued.
          glClearColor(cmd.clear.r, cmd.clear.g, cmd.clear.b, cmd.clear.a);
        }
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        break;

      case CommandType::ClearDepthBuffer:
        glClear(GL_DEPTH_BUFFER_BIT);
        break;

      case CommandType::ClearColorBuffer:
        glClearColor(cmd.clear.r, cmd.clear.g, cmd.clear.b, cmd.clear.a);
        glClear(GL_COLOR_BUFFER_BIT);
        break;

      case CommandType::ClearStencilBuffer:
        glClear(GL_STENCIL_BUFFER_BIT);
        break;

      case CommandType::ClearAll:
        glClearColor(cmd.clear.r, cmd.clear.g, cmd.clear.b, cmd.clear.a);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
                GL_STENCIL_BUFFER_BIT);
        break;

      case CommandType::Draw: {
        if (_activeProgram == 0 || _boundVao == 0) {
          Logger::LogWarning("Draw: missing valid shader or mesh; ignored");
          break;
        }
        GLenum mode = mapPrimitives(_currentGLState.primitives);
        glDrawArrays(mode,
                     static_cast<GLint>(cmd.draw.first),
                     static_cast<GLsizei>(cmd.draw.elementCount));
        break;
      }

      case CommandType::DrawIndexed: {
        if (_activeProgram == 0 || _boundVao == 0) {
          Logger::LogWarning(
            "DrawIndexed: missing valid shader or mesh; ignored");
          break;
        }
        GLenum mode = mapPrimitives(_currentGLState.primitives);
        const void* offset = reinterpret_cast<const void*>(
          static_cast<uintptr_t>(cmd.drawIndexed.firstIndex) *
          sizeof(unsigned int));
        glDrawElements(mode,
                       static_cast<GLsizei>(cmd.drawIndexed.elementCount),
                       GL_UNSIGNED_INT,
                       offset);
        break;
      }

      case CommandType::DrawInstanced: {
        if (_activeProgram == 0 || _boundVao == 0) {
          Logger::LogWarning(
            "DrawInstanced: missing valid shader or mesh; ignored");
          break;
        }
        GLenum mode = mapPrimitives(_currentGLState.primitives);
        glDrawArraysInstanced(
          mode,
          0,
          static_cast<GLsizei>(cmd.drawInstanced.elementCount),
          static_cast<GLsizei>(cmd.drawInstanced.instanceCount));
        break;
      }

      case CommandType::UpdateBuffer: {
        GLMesh* mesh = resolveMesh(tables, cmd.updateBuffer.handle);
        if (!mesh || !cmd.updateBuffer.data) {
          Logger::LogWarning("UpdateBuffer: invalid handle or null data");
          break;
        }
        mesh->UpdateVertexData(cmd.updateBuffer.data,
                               cmd.updateBuffer.sizeBytes,
                               cmd.updateBuffer.offsetBytes);
        break;
      }

      default:
        break;
    }
  }
}

HWInfo
GLDevice::GetHWInfo()
{
  HWInfo info;
  info.gpuVendor = (const char*)glGetString(GL_VENDOR);
  info.gpuRenderer = (const char*)glGetString(GL_RENDERER);
  info.glVersion = (const char*)glGetString(GL_VERSION);
  info.glslVersion = (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &info.maxTextureSize);
  glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &info.maxTextureSlots);
  glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &info.maxUniformBlockSize);
  glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &info.maxVertexAttributes);
  glGetIntegerv(GL_MAX_GEOMETRY_INPUT_COMPONENTS,
                &info.maxGeometryInputComponents);
  glGetIntegerv(GL_MAX_GEOMETRY_OUTPUT_COMPONENTS,
                &info.maxGeometryOutputComponents);
  glGetIntegerv(GL_MAX_GEOMETRY_TOTAL_OUTPUT_COMPONENTS,
                &info.maxGeometryTotalOutputComponents);
  glGetIntegerv(GL_MAX_TESS_CONTROL_INPUT_COMPONENTS,
                &info.maxTessControlInputComponents);
  glGetIntegerv(GL_MAX_TESS_CONTROL_OUTPUT_COMPONENTS,
                &info.maxTessControlOutputComponents);
  glGetIntegerv(GL_MAX_TESS_EVALUATION_INPUT_COMPONENTS,
                &info.maxTessEvaluationInputComponents);
  glGetIntegerv(GL_MAX_TESS_EVALUATION_OUTPUT_COMPONENTS,
                &info.maxTessEvaluationOutputComponents);

  const char* extensions = (const char*)glGetString(GL_EXTENSIONS);
  if (extensions) {
    std::string extStr(extensions);
    info.hasGeometryShaderSupport =
      extStr.find("GL_ARB_geometry_shader4") != std::string::npos ||
      extStr.find("GL_EXT_geometry_shader") != std::string::npos;
    info.hasTessellationShaderSupport =
      extStr.find("GL_ARB_tessellation_shader") != std::string::npos;
    info.hasComputeShaderSupport =
      extStr.find("GL_ARB_compute_shader") != std::string::npos ||
      extStr.find("GL_VERSION_4_3") != std::string::npos;
    info.hasRayTracingSupport =
      extStr.find("GL_NV_ray_tracing") != std::string::npos ||
      extStr.find("GL_KHR_ray_tracing") != std::string::npos;
    info.hasMeshShaderSupport =
      extStr.find("GL_ARB_mesh_shader") != std::string::npos;
  }

  if (info.glVersion.find("4.0") != std::string::npos) {
    info.hasTessellationShaderSupport = true;
  }
  if (info.glVersion.find("4.3") != std::string::npos ||
      info.glVersion.find("5") != std::string::npos) {
    info.hasComputeShaderSupport = true;
  }
  if (info.glVersion.find("4.6") != std::string::npos ||
      info.glVersion.find("5") != std::string::npos) {
    info.hasMeshShaderSupport = true;
  }

  glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS,
                &info.maxComputeWorkGroupInvocations);
  glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_COUNT, info.maxComputeWorkGroupCount);
  glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_SIZE, info.maxComputeWorkGroupSize);
  glGetIntegerv(GL_MAX_IMAGE_UNITS, &info.maxImageUnits);

  GLint totalMemoryKb = 0;
  glGetIntegerv(GL_GPU_MEM_INFO_TOTAL_AVAILABLE_MEM_NVX, &totalMemoryKb);
  if (totalMemoryKb > 0) {
    info.vramSize = totalMemoryKb / 1024;
  }

  glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &info.maxUniformBlocks);
  return info;
}
