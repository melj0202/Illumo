#pragma once
#include <Illumo/Rendering/PipelineState.h>
#include <Illumo/Rendering/ResourceHandle.h>
#include <cstddef>
#include <cstdint>

// Opaque, typed, generational backend handles (never raw GL object names).
// Data pointers in uniform/update payloads must remain valid until
// SubmitCommandQueue returns. Renderer copies matrix values into retained
// frame storage before enqueueing them.

enum class CommandType
{
  // Pipeline / raster state
  SetPipelineState,
  SetViewport,
  SetScissorState,

  // Resource bindings
  SetFramebuffer,
  SetShader,
  SetMesh,
  SetTexture,

  // Uniforms
  SetUniformInt,
  SetUniformFloat,
  SetUniformVec2,
  SetUniformVec3,
  SetUniformVec4,
  SetUniformMat4,

  // Resource updates (same-frame pointer validity)
  UpdateTexture,
  UpdateBuffer,
  UpdateIndexBuffer,

  // Clear
  ClearScreen,
  ClearDepthBuffer,
  ClearColorBuffer,
  ClearStencilBuffer,
  ClearAll,

  // Draw
  Draw,
  DrawIndexed,
  DrawInstanced,

  // GPU buffers and instancing (appended: existing values are unchanged)
  WriteBuffer,
  BindUniformBuffer,
  SetInstanceStream,
  DrawIndexedInstanced,

  // Runs a RecordedCommandList's tokens in place.
  ExecuteList,
};

class RecordedCommandList;

// Per-instance attribute layouts. Mesh attributes use locations 0-3;
// instance attributes start at location 4.
enum class InstanceLayout : unsigned char
{
  None = 0,
  // Model mat4 (locations 4-7), previous model mat4 (8-11), tint vec4 (12).
  LitModelTint = 1,
};

inline unsigned int
instanceLayoutStride(InstanceLayout layout)
{
  return layout == InstanceLayout::LitModelTint ? 144u : 0u;
}

struct CmdClearColor
{
  float r;
  float g;
  float b;
  float a;
};

struct CmdViewport
{
  int x;
  int y;
  int width;
  int height;
};

struct CmdScissor
{
  bool enabled;
  int x;
  int y;
  int width;
  int height;
};

struct CmdBindMesh
{
  MeshHandle handle;
};

struct CmdBindFramebuffer
{
  FramebufferHandle handle; // Invalid / default slot 0 binds screen (FBO 0)
};

struct CmdBindShader
{
  ShaderHandle handle;
};

struct CmdBindTexture
{
  TextureHandle handle;
  unsigned int slot;
};

// A uniform's identity, computed once when its token is built (D-R37): the
// FNV-1a hash of its whole name, "[n]" suffix included. Backends find a
// program's uniform by it instead of hashing the name on every token, and
// always confirm the name: a token built without the renderer may carry any
// key, and a lookup that misses re-derives the key from the name.
struct UniformKey
{
  std::uint32_t hash;
};

inline bool
operator==(const UniformKey& left, const UniformKey& right)
{
  return left.hash == right.hash;
}

inline UniformKey
uniformKeyOf(const char* name)
{
  std::uint32_t hash = 2166136261u;
  if (name != nullptr) {
    for (std::size_t index = 0u; name[index] != '\0'; ++index) {
      hash = (hash ^ static_cast<unsigned char>(name[index])) * 16777619u;
    }
  }
  return UniformKey{ hash };
}

// D-R9: string-named uniforms are GL-shaped debt; D-R37 adds
// RenderCommand::uniformKey so no backend hashes the name per token.
struct CmdUniformInt
{
  char name[32];
  int value;
};

struct CmdUniformFloat
{
  char name[32];
  float value;
};

struct CmdUniformVec2
{
  char name[32];
  float x;
  float y;
};

struct CmdUniformVec3
{
  char name[32];
  float x;
  float y;
  float z;
};

struct CmdUniformVec4
{
  char name[32];
  float x;
  float y;
  float z;
  float w;
};

struct CmdUniformMat4
{
  char name[32];
  const float* value;
};

struct CmdDraw
{
  unsigned int elementCount;
  unsigned int first;
};

struct CmdDrawIndexed
{
  unsigned int elementCount;
  unsigned int firstIndex;
};

struct CmdDrawInstanced
{
  unsigned int elementCount;
  unsigned int instanceCount;
};

struct CmdUpdateTexture
{
  TextureHandle handle;
  int x;
  int y;
  int width;
  int height;
  // 0 = use texture's create format; otherwise channel count hint (3=RGB,
  // 4=RGBA, 1=R)
  int channels;
  // Source buffer row length in *pixels*. 0 = tightly packed rows of `width`.
  // Use full texture width when `data` points into a larger staging buffer
  // (dirty rects).
  int srcRowStride;
  const void* data;
};

struct CmdUpdateBuffer
{
  MeshHandle handle;
  unsigned int offsetBytes;
  unsigned int sizeBytes;
  const void* data;
};

// A write at offset zero may discard the buffer's previous contents first.
struct CmdWriteBuffer
{
  BufferHandle handle;
  unsigned int offsetBytes;
  unsigned int sizeBytes;
  const void* data;
};

struct CmdBindUniformBuffer
{
  BufferHandle handle;
  unsigned int binding;
};

// Attaches an Instance buffer to the currently bound mesh's vertex arrays.
struct CmdInstanceStream
{
  BufferHandle handle;
  unsigned int offsetBytes;
  InstanceLayout layout;
};

struct CmdDrawIndexedInstanced
{
  unsigned int elementCount;
  unsigned int firstIndex;
  unsigned int instanceCount;
};

// The list must stay alive and unchanged until submission returns.
struct CmdExecuteList
{
  const RecordedCommandList* list;
};

// Tagged-union command token. Trivially copyable for the vector queue.
struct RenderCommand
{
  CommandType commandType = CommandType::ClearScreen;
  // Meaningful for SetPipelineState (and optionally read as current state
  // seed).
  PipelineState pipelineState;

  union
  {
    CmdClearColor clear;
    CmdViewport viewport;
    CmdScissor scissor;
    CmdBindFramebuffer bindFramebuffer;
    CmdBindMesh bindMesh;
    CmdBindShader bindShader;
    CmdBindTexture bindTexture;
    CmdUniformInt uniformInt;
    CmdUniformFloat uniformFloat;
    CmdUniformVec2 uniformVec2;
    CmdUniformVec3 uniformVec3;
    CmdUniformVec4 uniformVec4;
    CmdUniformMat4 uniformMat4;
    CmdDraw draw;
    CmdDrawIndexed drawIndexed;
    CmdDrawInstanced drawInstanced;
    CmdUpdateTexture updateTexture;
    CmdUpdateBuffer updateBuffer;
    CmdUpdateBuffer updateIndexBuffer;
    CmdWriteBuffer writeBuffer;
    CmdBindUniformBuffer bindUniformBuffer;
    CmdInstanceStream instanceStream;
    CmdDrawIndexedInstanced drawIndexedInstanced;
    CmdExecuteList executeList;
  };
  // Append fields to preserve existing positional aggregate initialization.
  // ClearDepthBuffer, ClearScreen and ClearAll default to the far depth.
  float clearDepthValue = 1.0f;
  // The SetUniform* name's key (D-R37), set by the Renderer push helpers. It
  // sits in what was tail padding, so the token stays 72 bytes.
  UniformKey uniformKey{ 0u };
};
