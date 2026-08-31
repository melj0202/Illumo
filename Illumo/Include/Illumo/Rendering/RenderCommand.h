#pragma once
#include <Illumo/Rendering/PipelineState.h>
#include <Illumo/Rendering/ResourceHandle.h>

// Opaque, typed, generational backend handles (never raw GL object names).
// Data pointers in update payloads must remain valid until SubmitCommandQueue
// returns.

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
};

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

// D-R9: string-named uniforms are GL-shaped debt. Fine for OpenGL +
// MockBackend. A second real API (Metal/Vulkan) would want locations / binding
// points instead.
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
  float m[16];
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
  };
};
