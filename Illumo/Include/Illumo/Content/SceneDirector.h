#pragma once

#include <Illumo/Content/ProgramScene.h>
#include <Illumo/Rendering/Camera.h>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// How a switch reaches the next scene.
enum class SceneSwitch
{
  // At the next frame boundary.
  Cut,
  // Once the program reports its transition cover fully drawn
  // (SceneDirector::coverComplete); the outgoing scene keeps running until
  // then.
  Cover
};

// Render worlds for scenes, where the host keeps one per scene (frame schema
// v8). Without one, every scene shares IllumoContext::renderWorld.
class ISceneWorlds
{
public:
  virtual ~ISceneWorlds() = default;
  // A new, empty world for a starting scene, or nullptr for none.
  virtual IRenderWorld* create() = 0;
  // The world the frame composes from now on (nullptr for none).
  virtual void activate(IRenderWorld* world) = 0;
  // Destroys a world create() returned; its host objects go with it.
  virtual void destroy(IRenderWorld* world) = 0;
};

// Owns a program's scenes and switches between them at frame boundaries
// (D-E31). Only the active scene updates, receives input and draws; the
// others are kept, frozen, until released. Each scene keeps its own camera:
// the shared IllumoContext::camera is saved when a scene leaves and restored
// when it re-enters, and a scene starts from the camera the director was
// created with. Main-thread only.
class SceneDirector
{
public:
  // `context` must outlive the director. Its camera, when set, is the one
  // new scenes start from.
  explicit SceneDirector(IllumoContext& context,
                         ISceneWorlds* worlds = nullptr);
  // Stops every started scene, the active one first, then the rest in
  // reverse order of adding.
  ~SceneDirector();
  SceneDirector(const SceneDirector&) = delete;
  SceneDirector& operator=(const SceneDirector&) = delete;
  SceneDirector(SceneDirector&&) = delete;
  SceneDirector& operator=(SceneDirector&&) = delete;

  // Takes ownership. False for an empty name, a null scene or a name in use.
  // The scene starts the first time it is entered.
  bool add(std::string name, std::unique_ptr<ProgramScene> scene);
  bool has(std::string_view name) const;
  ProgramScene* find(std::string_view name);
  ProgramScene* active();
  // Empty when no scene is active.
  const std::string& activeName() const;

  // Requests a switch, replacing any pending one. False (nothing pending)
  // for an unknown name or the active scene.
  bool switchTo(std::string_view name, SceneSwitch how = SceneSwitch::Cut);
  bool hasPendingSwitch() const { return m_pending != nullptr; }
  // A Cover switch is waiting for coverComplete().
  bool covering() const
  {
    return m_pending != nullptr && m_pendingHow == SceneSwitch::Cover &&
           !m_covered;
  }
  // The program's cover is fully drawn: the Cover switch applies at the
  // next frame boundary.
  void coverComplete() { m_covered = true; }
  // Stops and destroys a kept scene. Refused (false) for the active scene
  // and for the target of a pending switch.
  bool release(std::string_view name);

  // Frame boundary: applies a due switch. A target that fails to start is
  // logged and the previous scene re-entered; false means there was none to
  // return to, and the program should close.
  bool applyPending();
  // The active scene, when there is one.
  void update(double elapsed);
  void dispatch(Scene& frame);
  bool closeRequested();
  // Stops every started scene (as the destructor does). Idempotent.
  void stopAll() noexcept;

private:
  struct Entry
  {
    std::string name;
    std::unique_ptr<ProgramScene> scene;
    bool started = false;
    // The camera as the scene left it, restored when it re-enters.
    Camera camera;
    bool hasCamera = false;
    // Owned through m_worlds when it created one.
    IRenderWorld* ownWorld = nullptr;
  };

  Entry* entry(std::string_view name);
  const Entry* entry(std::string_view name) const;
  bool startEntry(Entry& target);
  void enterEntry(Entry& target);
  void leaveEntry(Entry& current);
  void stopEntry(Entry& current) noexcept;
  void resetFrameInput();

  IllumoContext& m_context;
  ISceneWorlds* m_worlds;
  // The camera new scenes start from.
  Camera m_initialCamera;
  // The world scenes share when no ISceneWorlds gives them their own.
  IRenderWorld* m_sharedWorld;
  std::vector<std::unique_ptr<Entry>> m_entries;
  Entry* m_active = nullptr;
  Entry* m_pending = nullptr;
  SceneSwitch m_pendingHow = SceneSwitch::Cut;
  bool m_covered = false;
};
