#pragma once

#include <IllumoGuest/Frame.h>
#include <memory>
#include <string>

class Renderer;
class Scene;

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
  bool accept(std::span<const std::byte> packet);
  void dispatch(Scene& scene);
  void retire();
  const std::string& error() const;

private:
  struct State;
  std::unique_ptr<State> m_state;
};
