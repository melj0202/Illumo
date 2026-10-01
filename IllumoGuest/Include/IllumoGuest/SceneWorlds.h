#pragma once

#include <Illumo/Content/SceneDirector.h>
#include <IllumoGuest/RenderWorld.h>
#include <memory>
#include <vector>

// The host render worlds of a scene program (frame schema v8, D-R30): one
// GuestRenderWorld per scene, sent over the frame's one operation stream.
// Each world's operations follow a SelectWorld naming it, and the active
// scene's world is the one shown, so a kept scene's world stays on the host,
// intact and undrawn. World ids are never reused; the first is 1, the world a
// version 7 host already had. A guest keeps at most kWorlds worlds; a scene
// started beyond that gets none and draws its lit meshes as MeshVisuals.
class GuestSceneWorlds final : public ISceneWorlds
{
public:
  static constexpr std::size_t kWorlds = 8;

  explicit GuestSceneWorlds(GuestRecordingBackend& backend);
  GuestSceneWorlds(const GuestSceneWorlds&) = delete;
  GuestSceneWorlds& operator=(const GuestSceneWorlds&) = delete;

  IRenderWorld* create() override;
  // The shown world also receives the recorder's sky.
  void activate(IRenderWorld* world) override;
  void destroy(IRenderWorld* world) override;

  // Starts a frame: the shown world has no sky until one is set.
  void beginFrame();
  // Moves at most `limit` operations into `output`: destroyed worlds first,
  // then each world with something to send behind its SelectWorld, then a
  // ShowWorld when the shown world changed. What does not fit stays queued.
  void takeOperations(std::vector<GuestWorldOperation>& output,
                      std::size_t limit);
  std::size_t worldCount() const { return m_worlds.size(); }

private:
  struct World
  {
    std::uint32_t id = 0;
    std::unique_ptr<GuestRenderWorld> world;
    // The host has created it (a SelectWorld naming it was sent).
    bool announced = false;
  };

  GuestRecordingBackend& m_backend;
  std::vector<World> m_worlds;
  std::vector<std::uint32_t> m_destroyed;
  GuestRenderWorld* m_active = nullptr;
  std::uint32_t m_activeId = 0;
  // A host shows world 1 until told otherwise.
  std::uint32_t m_shownOnHost = 1;
  std::uint32_t m_nextId = 1;
};
