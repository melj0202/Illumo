#pragma once

#include <Illumo/Content/SceneInstance.h>
#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Services/CommandRegistry.h>
#include <memory>
#include <string>
#include <vector>

class SceneDirector;

// One screen of a program (D-E31): its own logic, UI and console commands,
// backed by a SceneInstance and, where the host keeps one per scene, its own
// render world. A SceneDirector owns scenes; only the active scene updates,
// receives input and draws. A scene the program leaves is kept, frozen,
// until the program releases it, and resumes where it was.
//
// Lifecycle: start once (the first time it is entered), then enter and
// leave any number of times, then stop once. update and dispatch run only
// between enter and leave. Main-thread only, like SceneInstance.
class ProgramScene
{
public:
  ProgramScene();
  virtual ~ProgramScene();
  ProgramScene(const ProgramScene&) = delete;
  ProgramScene& operator=(const ProgramScene&) = delete;
  ProgramScene(ProgramScene&&) = delete;
  ProgramScene& operator=(ProgramScene&&) = delete;

  // Once, before the first enter; content(), world() and context() are
  // ready. Returning false (or throwing) fails the switch to this scene.
  virtual bool start(IllumoContext& context) = 0;
  // Each time the scene becomes active, right after start the first time.
  // Register console commands here (with command()).
  virtual void enter() {}
  // Each time it stops being active. Park workers and timers; everything
  // else is kept. Commands registered with command() are withdrawn after.
  virtual void leave() {}
  virtual void update(double elapsed) = 0;
  virtual void dispatch(Scene& frame) = 0;
  // Once, when the program releases the scene or stops. Only a started
  // scene is stopped.
  virtual void stop() = 0;
  // Main-thread close negotiation for the active scene. Return false to
  // keep running, then request close again once any confirmation is done.
  virtual bool closeRequested() { return true; }
  // Options for this scene's SceneInstance (editors want pick proxies).
  virtual SceneInstanceOptions contentOptions() const { return {}; }

  // The scene's content, from start until the scene is destroyed. Empty
  // unless the scene loads a document.
  SceneInstance& content() { return *m_content; }
  const SceneInstance& content() const { return *m_content; }
  // The render world this scene's content draws through, or nullptr when
  // the host keeps none (then lit meshes draw as MeshVisuals).
  IRenderWorld* world() const { return m_world; }

protected:
  IllumoContext& context() { return *m_context; }
  // Registers a console command for as long as the scene is active: it is
  // withdrawn when the scene leaves or stops. Call from enter().
  void command(const std::string& name,
               CommandFn function,
               const std::string& usage = "",
               const std::string& description = "",
               const std::vector<std::string>& completions = {});
  // Updates the content and adds its drawable (and sky) to the world layer.
  void dispatchContent(Scene& frame);

private:
  friend class SceneDirector;
  void withdrawCommands();

  IllumoContext* m_context = nullptr;
  std::unique_ptr<SceneInstance> m_content;
  IRenderWorld* m_world = nullptr;
  std::vector<std::string> m_commands;
};
