#pragma once
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/RenderLayerId.h>
#include <array>
#include <vector>

class IRenderWindow;

// FrameRenderList (kept as type name Scene for source stability).
//
// Role: ordered, non-owning layered list of drawables rebuilt every frame by
// modules via IModule::DispatchDrawables (World → UI → Debug). This is NOT a
// retained scene graph, spatial hierarchy, or world container (D-E4). A
// persistent SceneGraph may appear here as one drawable (D-E8). One main pass;
// layers are composition order, not GPU render passes (D-R14).
class Scene
{
public:
  Scene(IRenderWindow* window, Camera* camera)
    : window(window)
    , activeCamera(camera)
  {
  }

  ~Scene() = default;

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

  IRenderWindow* window;
  Camera* activeCamera;

private:
  std::array<std::vector<DrawableBase*>,
             static_cast<size_t>(RenderLayerId::Count)>
    layers;
};
