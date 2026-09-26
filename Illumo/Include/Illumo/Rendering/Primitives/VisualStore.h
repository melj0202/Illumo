#pragma once
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/RecordedCommandList.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>

class Camera;
class IRenderWindow;
class Renderer;

using VisualId = uint32_t;

// Where and how a stored visual draws. Mirrors the GameVisual setters.
struct VisualProperties
{
  PrimitiveSpace space = PrimitiveSpace::Pixels;
  RenderLayerId layer = RenderLayerId::World;
  Transform2D transform;
  float opacity = 1.0f;
  bool clipEnabled = false;
  Rect2 clipRect;
  bool visible = true;
};

// Host-retained 2D visuals (D-E30, host-render-world design 6.9). Each visual
// is a GameVisual edited in place by id, so tessellation, text layout,
// ordering and batching are the native code. Its draw tokens are recorded
// once and replayed with ExecuteList until its geometry or frame state
// (resolution, projection, clip) changes. Main-thread only; call
// releaseResources before the renderer goes away.
class VisualStore
{
public:
  struct Limits
  {
    size_t visuals = 4096;
    size_t itemsPerVisual = 65536;
  };
  enum class Result : uint8_t
  {
    Ok,
    UnknownId,
    DuplicateId,
    OverBudget,
    BadIndex
  };
  struct Stats
  {
    size_t visuals = 0;
    // Lifetime counts of recorded lists and of replays without recording.
    size_t recordings = 0;
    size_t replays = 0;
  };

  explicit VisualStore(Limits limits);
  VisualStore();
  ~VisualStore();
  VisualStore(const VisualStore&) = delete;
  VisualStore& operator=(const VisualStore&) = delete;
  VisualStore(VisualStore&&) = delete;
  VisualStore& operator=(VisualStore&&) = delete;

  // Shared by every visual: the window sizes Pixels space, the camera
  // projects World space.
  void setRenderer(Renderer* renderer);
  void setWindow(IRenderWindow* window);
  void setCamera(Camera* camera);

  Result create(VisualId id);
  Result destroy(VisualId id);
  Result setProperties(VisualId id, const VisualProperties& properties);
  // Index itemCount() appends; see GameVisual::setItem.
  Result setItem(VisualId id, size_t index, const GameVisualItem& item);
  // Typed forms: replacing an item of the same kind reuses its storage.
  Result setItem(VisualId id, size_t index, const ShapePrimitive& item);
  Result setItem(VisualId id, size_t index, const SpritePrimitive& item);
  Result setItem(VisualId id, size_t index, const TextPrimitive& item);
  Result removeItems(VisualId id, size_t first, size_t count);
  Result clearItems(VisualId id);

  bool contains(VisualId id) const;
  const VisualProperties* properties(VisualId id) const;
  const GameVisual* visual(VisualId id) const;

  // Queues the visual at this point of the frame: uploads changed geometry,
  // re-records when needed, then one ExecuteList. Invisible and empty
  // visuals queue nothing. False on a renderer error (reported as a frame
  // error) or an unknown id. `frame` replaces the shared window and camera.
  bool append(Renderer* renderer,
              VisualId id,
              const GameVisual::FrameOverride* frame = nullptr);

  // Destroys every visual and its meshes.
  void releaseResources();

  const Stats& stats() const { return m_stats; }
  const Limits& limits() const { return m_limits; }

private:
  struct Entry
  {
    std::unique_ptr<GameVisual> visual;
    VisualProperties properties;
    RecordedCommandList list;
    GameVisual::FrameState recordedState;
    bool recorded = false;
  };

  template<typename Item>
  Result placeItem(VisualId id, size_t index, const Item& item);
  Entry* find(VisualId id);
  const Entry* find(VisualId id) const;

  Limits m_limits;
  Renderer* m_renderer = nullptr;
  IRenderWindow* m_window = nullptr;
  Camera* m_camera = nullptr;
  std::unordered_map<VisualId, std::unique_ptr<Entry>> m_entries;
  Stats m_stats;
};
