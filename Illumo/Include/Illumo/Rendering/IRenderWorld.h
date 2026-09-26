#pragma once
#include <Illumo/Foundation/AxisAlignedBounds3.h>
#include <Illumo/Rendering/ResourceHandle.h>
#include <array>
#include <cstdint>

// Persistent world render objects owned by whoever renders them. Callers
// choose ids (nonzero, unique per kind) so a remote caller, such as a WASM
// guest, can create an object and use it in the same step without a reply.
// Every call validates its arguments and returns false without changing
// anything when they are rejected.
using RenderMaterialId = uint32_t;
using RenderInstanceId = uint32_t;

// An id no other caller in this process has taken, for callers that share one
// world (one guest world serves every scene of its modules). Main-thread only.
inline uint32_t
nextRenderWorldId()
{
  static uint32_t next = 0;
  return ++next;
}

struct RenderMaterialDesc
{
  std::array<float, 4> tint{ 1.0f, 1.0f, 1.0f, 1.0f };
  bool receivesShadow = true;
  bool castsShadow = true;
  // Blended instances draw one at a time, after opaque ones, in creation
  // order.
  bool blend = false;
};

struct RenderInstanceDesc
{
  MeshHandle mesh{};
  uint32_t firstIndex = 0;
  uint32_t indexCount = 0;
  RenderMaterialId material = 0;
  std::array<float, 16> world{ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                               0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
  std::array<float, 4> tint{ 1.0f, 1.0f, 1.0f, 1.0f };
  bool visible = true;
  // Mesh-local bounds. Without them an instance is never culled and does not
  // take part in shadow fitting (unknown bounds fail open).
  bool hasBounds = false;
  AxisAlignedBounds3 localBounds{};
};

struct RenderEnvironment
{
  std::array<float, 3> lightDirection{ 0.5f, 1.0f, 0.3f };
  std::array<float, 3> lightColor{ 1.0f, 1.0f, 1.0f };
  std::array<float, 3> ambientColor{ 0.2f, 0.2f, 0.2f };
  bool shadowsEnabled = false;
  float shadowBias = 0.002f;
  float shadowSlopeScale = 0.01f;
  float shadowNormalOffset = 0.02f;
  bool shadowPcf = false;
  int shadowMapSize = 1024;
  float shadowMinimumRadius = 2.5f;
  float shadowLightDistance = 8.0f;
  float shadowCasterDistance = 100.0f;
};

// The world's background: a cubemap drawn behind everything, oriented by the
// frame camera (the SkyboxVisual look). An invalid cubemap means no sky.
struct RenderSkyboxDesc
{
  TextureHandle cubemap{};
  std::array<float, 4> tint{ 1.0f, 1.0f, 1.0f, 1.0f };
};

class IRenderWorld
{
public:
  virtual ~IRenderWorld() = default;

  virtual bool createMaterial(RenderMaterialId id,
                              const RenderMaterialDesc& desc) = 0;
  virtual bool updateMaterial(RenderMaterialId id,
                              const RenderMaterialDesc& desc) = 0;
  // Refused while any instance still uses the material.
  virtual bool destroyMaterial(RenderMaterialId id) = 0;

  virtual bool createInstance(RenderInstanceId id,
                              const RenderInstanceDesc& desc) = 0;
  virtual bool setInstanceTransform(RenderInstanceId id,
                                    const std::array<float, 16>& world) = 0;
  virtual bool setInstanceTint(RenderInstanceId id,
                               const std::array<float, 4>& tint) = 0;
  virtual bool setInstanceVisible(RenderInstanceId id, bool shown) = 0;
  virtual bool destroyInstance(RenderInstanceId id) = 0;

  virtual void setEnvironment(const RenderEnvironment& environment) = 0;
  // False, with nothing changed, for a non-finite tint.
  virtual bool setSkybox(const RenderSkyboxDesc& skybox) = 0;
};
