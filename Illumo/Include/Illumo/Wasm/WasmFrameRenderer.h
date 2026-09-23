#pragma once

#include <IllumoGuest/Frame.h>
#include <IllumoGuest/Services.h>
#include <memory>
#include <string>

class Renderer;
class Scene;

// Diagnostics: the last accepted frame's payload plus lifetime totals of host
// mesh slot allocations. Counting only; nothing here affects rendering.
struct WasmFrameCounters
{
  std::uint64_t batches = 0;
  std::uint64_t retainedBatches = 0;
  std::uint64_t inlineVertexBytes = 0;
  std::uint64_t inlineIndexBytes = 0;
  std::uint64_t textureWrites = 0;
  std::uint64_t textureWriteBytes = 0;
  std::uint64_t meshWriteBytes = 0;
  std::uint64_t meshEnrollments = 0;
  std::uint64_t meshReplacements = 0;
};

// Main-thread renderer adapter. Accept between synchronous frame submissions.
// All geometry/layout arrives from WASM; this class only validates, retains,
// enrolls backend resources and translates batches into Renderer tokens.
class WasmFrameRenderer
{
public:
  WasmFrameRenderer(Renderer& renderer,
                    std::uint64_t owner,
                    GuestFrameLimits limits = {});
  ~WasmFrameRenderer();
  WasmFrameRenderer(const WasmFrameRenderer&) = delete;
  WasmFrameRenderer& operator=(const WasmFrameRenderer&) = delete;
  WasmFrameRenderer(WasmFrameRenderer&&) = delete;
  WasmFrameRenderer& operator=(WasmFrameRenderer&&) = delete;

  GuestResourceId createTexture(std::span<const std::byte> pixels,
                                std::uint32_t width,
                                std::uint32_t height,
                                std::uint32_t channels,
                                bool linear);
  bool releaseTexture(const GuestResourceId& id);
  // Six square RGBA faces; sampled by Skybox batches (frame schema v3).
  GuestResourceId createCubemap(std::span<const std::byte> faces,
                                std::uint32_t size);
  // Retained meshes (frame schema v3): reserve, fill in order, then draw by
  // id. The write that completes the mesh validates it and enrolls it.
  GuestResourceId createMesh(const GuestMeshRequest& request);
  bool writeMesh(const GuestMeshWrite& write);
  bool releaseMesh(const GuestResourceId& id);
  bool accept(std::span<const std::byte> packet);
  void dispatch(Scene& scene);
  void retire();
  const std::string& error() const;
  const WasmFrameCounters& counters() const;

private:
  struct State;
  std::unique_ptr<State> m_state;
};
