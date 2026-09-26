#pragma once
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/RenderLayerId.h>
#include <Illumo/Rendering/RenderPass.h>
#include <array>
#include <utility>
#include <vector>

class IRenderWindow;

// The frame's draw list (named Scene until D-E31, when "scene" came to mean a
// program's ProgramScene).
//
// Role: ordered, non-owning layered list of drawables rebuilt every frame by
// the program and the debug overlay as they dispatch (World → UI → Debug). Each
// layer can define an ordered sequence of RenderPass descriptors (defaults to
// a single DrawPass to the main backbuffer if unspecified).
class DrawList
{
public:
  DrawList(IRenderWindow* window = nullptr, Camera* camera = nullptr)
    : window(window)
    , activeCamera(camera)
  {
  }

  ~DrawList() = default;

  void AddDrawable(DrawableBase* drawable,
                   RenderLayerId layer = RenderLayerId::World)
  {
    if (!drawable) {
      return;
    }
    const unsigned index = renderLayerIndex(layer);
    if (index >= renderLayerCount()) {
      return;
    }
    layers[index].push_back(drawable);
  }

  void ClearDrawables()
  {
    for (unsigned i = 0; i < renderLayerCount(); ++i) {
      layers[i].clear();
    }
  }

  const std::vector<DrawableBase*>& drawablesIn(RenderLayerId layer) const
  {
    const unsigned index = renderLayerIndex(layer);
    if (index >= renderLayerCount()) {
      return layers[0];
    }
    return layers[index];
  }

  size_t drawableCount() const
  {
    size_t total = 0;
    for (unsigned i = 0; i < renderLayerCount(); ++i) {
      total += layers[i].size();
    }
    return total;
  }

  void SetLayerPasses(RenderLayerId layer, std::vector<RenderPassDesc> passes)
  {
    const unsigned index = renderLayerIndex(layer);
    if (index < renderLayerCount()) {
      m_layerPasses[index] = std::move(passes);
    }
  }

  const std::vector<RenderPassDesc>& passesIn(RenderLayerId layer) const
  {
    const unsigned index = renderLayerIndex(layer);
    if (index >= renderLayerCount()) {
      return m_layerPasses[0];
    }
    return m_layerPasses[index].empty() ? m_defaultLayerPasses[index]
                                        : m_layerPasses[index];
  }

  bool hasCustomPasses(RenderLayerId layer) const
  {
    const unsigned index = renderLayerIndex(layer);
    return (index < renderLayerCount()) && !passesIn(layer).empty();
  }

  // Host defaults are fallbacks; nonempty application passes take precedence.
  void SetDefaultLayerPasses(RenderLayerId layer,
                             std::vector<RenderPassDesc> passes)
  {
    const unsigned index = renderLayerIndex(layer);
    if (index < renderLayerCount()) {
      m_defaultLayerPasses[index] = std::move(passes);
    }
  }

  void ClearLayerPasses(RenderLayerId layer)
  {
    const unsigned index = renderLayerIndex(layer);
    if (index < renderLayerCount()) {
      m_layerPasses[index].clear();
    }
  }

  void ResetDefaultPasses()
  {
    for (unsigned i = 0; i < renderLayerCount(); ++i) {
      m_layerPasses[i].clear();
    }
  }

  IRenderWindow* window;
  Camera* activeCamera;

private:
  std::array<std::vector<DrawableBase*>,
             static_cast<size_t>(RenderLayerId::Count)>
    layers;
  std::array<std::vector<RenderPassDesc>,
             static_cast<size_t>(RenderLayerId::Count)>
    m_layerPasses;
  std::array<std::vector<RenderPassDesc>,
             static_cast<size_t>(RenderLayerId::Count)>
    m_defaultLayerPasses;
};
