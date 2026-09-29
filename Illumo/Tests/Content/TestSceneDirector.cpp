#include <Illumo/Content/SceneDirector.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/DrawList.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <memory>
#include <string>
#include <vector>

// What a scene saw, kept outside it so tests can read it after destruction.
struct SceneProbe
{
  int starts = 0;
  int enters = 0;
  int leaves = 0;
  int updates = 0;
  int draws = 0;
  int stops = 0;
  int destructions = 0;
  bool failStart = false;
  bool allowClose = true;
  // Registered from enter() through ProgramScene::command.
  std::string command;
  IRenderWorld* worldAtStart = nullptr;
  std::vector<std::string>* log = nullptr;
};

class ProbeScene final : public ProgramScene
{
public:
  ProbeScene(std::string name, SceneProbe& probe)
    : m_name(std::move(name))
    , m_probe(probe)
  {
  }
  ~ProbeScene() override
  {
    ++m_probe.destructions;
    note("destroy");
  }
  bool start(IllumoContext& context) override
  {
    (void)context;
    ++m_probe.starts;
    note("start");
    m_probe.worldAtStart = world();
    return !m_probe.failStart;
  }
  void enter() override
  {
    ++m_probe.enters;
    note("enter");
    if (!m_probe.command.empty()) {
      command(m_probe.command, [](const std::vector<std::string>&) {});
    }
  }
  void leave() override
  {
    ++m_probe.leaves;
    note("leave");
  }
  void update(double elapsed) override
  {
    (void)elapsed;
    ++m_probe.updates;
  }
  void dispatch(DrawList& frame) override
  {
    (void)frame;
    ++m_probe.draws;
  }
  void stop() override
  {
    ++m_probe.stops;
    note("stop");
  }
  bool closeRequested() override { return m_probe.allowClose; }

private:
  void note(const char* event)
  {
    if (m_probe.log != nullptr) {
      m_probe.log->push_back(m_name + ":" + event);
    }
  }
  std::string m_name;
  SceneProbe& m_probe;
};

// Accepts everything; scenes here only need a world to exist.
class NullWorld final : public IRenderWorld
{
public:
  bool createMaterial(RenderMaterialId, const RenderMaterialDesc&) override
  {
    return true;
  }
  bool updateMaterial(RenderMaterialId, const RenderMaterialDesc&) override
  {
    return true;
  }
  bool destroyMaterial(RenderMaterialId) override { return true; }
  bool createInstance(RenderInstanceId, const RenderInstanceDesc&) override
  {
    return true;
  }
  bool setInstanceTransform(RenderInstanceId,
                            const std::array<float, 16>&) override
  {
    return true;
  }
  bool setInstanceTint(RenderInstanceId, const std::array<float, 4>&) override
  {
    return true;
  }
  bool setInstanceVisible(RenderInstanceId, bool) override { return true; }
  bool destroyInstance(RenderInstanceId) override { return true; }
  void setEnvironment(const RenderEnvironment&) override {}
  bool setSkybox(const RenderSkyboxDesc&) override { return true; }
};

class FakeWorlds final : public ISceneWorlds
{
public:
  IRenderWorld* create() override
  {
    worlds.push_back(std::make_unique<NullWorld>());
    ++created;
    return worlds.back().get();
  }
  void activate(IRenderWorld* world) override { activeWorld = world; }
  void destroy(IRenderWorld* world) override
  {
    ++destroyed;
    destroyedWorlds.push_back(world);
  }
  std::vector<std::unique_ptr<NullWorld>> worlds;
  std::vector<IRenderWorld*> destroyedWorlds;
  IRenderWorld* activeWorld = nullptr;
  int created = 0;
  int destroyed = 0;
};

struct DirectorFixture
{
  DirectorFixture()
    : camera(glm::vec2(0.0f, 0.0f), 1.0f, nullptr)
    , input(nullptr)
  {
    context.camera = &camera;
    context.commandRegistry = &commands;
    context.inputManager = &input;
  }
  Camera camera;
  CommandRegistry commands;
  InputManager input;
  IllumoContext context;
};

static bool
addProbe(SceneDirector& director, const char* name, SceneProbe& probe)
{
  return director.add(name, std::make_unique<ProbeScene>(name, probe));
}

static int
testLifecycle()
{
  TestCounters c;
  DirectorFixture fixture;
  SceneProbe title;
  SceneProbe canvas;
  SceneProbe refused;
  {
    SceneDirector director(fixture.context);
    testTrue(c,
             addProbe(director, "title", title) &&
               addProbe(director, "canvas", canvas),
             "scenes are added by name");
    testTrue(c,
             !addProbe(director, "title", refused) && !director.add("", nullptr),
             "a taken name, an empty name or no scene is refused");
    testTrue(c,
             title.starts == 0 && director.active() == nullptr,
             "adding starts nothing");

    testTrue(c, director.switchTo("title"), "a switch is requested");
    director.update(0.016);
    testTrue(c,
             title.starts == 0 && director.active() == nullptr,
             "the switch waits for the frame boundary");
    testTrue(c, director.applyPending(), "the boundary applies it");
    testTrue(c,
             title.starts == 1 && title.enters == 1 &&
               director.activeName() == "title",
             "the first switch starts and enters the scene");
    director.update(0.016);
    testTrue(c,
             title.updates == 1 && canvas.updates == 0,
             "only the active scene updates");
    testTrue(c,
             !director.switchTo("title") && !director.switchTo("missing"),
             "switching to the active or an unknown scene is refused");

    director.switchTo("canvas");
    director.applyPending();
    testTrue(c,
             title.leaves == 1 && title.stops == 0 && canvas.starts == 1 &&
               canvas.enters == 1,
             "leaving keeps the scene; the new one starts and enters");
    director.update(0.016);
    testTrue(c,
             title.updates == 1 && canvas.updates == 1,
             "a kept scene is frozen");

    director.switchTo("title");
    director.applyPending();
    testTrue(c,
             title.starts == 1 && title.enters == 2 && canvas.leaves == 1,
             "resuming enters without starting again");

    testTrue(c, !director.release("title"), "the active scene is not released");
    director.switchTo("canvas");
    testTrue(c,
             !director.release("canvas"),
             "the target of a pending switch is not released");
    director.applyPending();
    testTrue(c,
             director.activeName() == "canvas",
             "the pending switch still applies");
    director.switchTo("title");
    director.applyPending();
    const bool released = director.release("canvas");
    testTrue(c,
             released && canvas.stops == 1 &&
               canvas.destructions == 1 && !director.has("canvas"),
             "releasing a kept scene stops and destroys it");
  }
  testTrue(c,
           title.leaves == 3 && title.stops == 1 && title.destructions == 1,
           "the director leaves, stops and destroys the active scene last");
  return c.failures;
}

static int
testFailedStart()
{
  TestCounters c;
  DirectorFixture fixture;
  SceneProbe first;
  SceneProbe broken;
  broken.failStart = true;
  SceneDirector director(fixture.context);
  addProbe(director, "first", first);
  addProbe(director, "broken", broken);
  director.switchTo("first");
  director.applyPending();

  director.switchTo("broken");
  testTrue(c,
           director.applyPending() && director.activeName() == "first" &&
             first.enters == 2 && broken.starts == 1 && broken.stops == 0,
           "a failed start re-enters the previous scene");
  broken.failStart = false;
  director.switchTo("broken");
  director.applyPending();
  testTrue(c,
           broken.starts == 2 && director.activeName() == "broken",
           "a scene that failed can start on a later switch");

  DirectorFixture lone;
  SceneProbe only;
  only.failStart = true;
  SceneDirector empty(lone.context);
  addProbe(empty, "only", only);
  empty.switchTo("only");
  testTrue(c,
           !empty.applyPending() && empty.active() == nullptr,
           "a failed first scene asks the program to close");
  return c.failures;
}

static int
testCoverSwitch()
{
  TestCounters c;
  DirectorFixture fixture;
  SceneProbe canvas;
  SceneProbe title;
  SceneDirector director(fixture.context);
  addProbe(director, "canvas", canvas);
  addProbe(director, "title", title);
  director.switchTo("canvas");
  director.applyPending();

  director.switchTo("title", SceneSwitch::Cover);
  testTrue(c, director.covering(), "a cover switch waits for the cover");
  director.applyPending();
  director.update(0.016);
  testTrue(c,
           director.activeName() == "canvas" && canvas.updates == 1 &&
             title.starts == 0,
           "the outgoing scene keeps running under the cover");
  director.coverComplete();
  testTrue(c, !director.covering(), "a complete cover is no longer pending");
  director.applyPending();
  testTrue(c,
           director.activeName() == "title" && title.enters == 1,
           "the switch applies once the cover is drawn");
  return c.failures;
}

static int
testFrameInputAndCommands()
{
  TestCounters c;
  DirectorFixture fixture;
  SceneProbe menu;
  SceneProbe game;
  menu.command = "play";
  game.command = "pause";
  SceneDirector director(fixture.context);
  addProbe(director, "menu", menu);
  addProbe(director, "game", game);
  director.switchTo("menu");
  director.applyPending();
  testTrue(c,
           fixture.commands.HasCommand("play") &&
             !fixture.commands.HasCommand("pause"),
           "the active scene's commands are registered");

  fixture.input.getKeyQueue().push({ KeyCode::Enter, InputAction::Press, 0 });
  fixture.input.getCharQueue().push('x');
  director.switchTo("game");
  director.applyPending();
  testTrue(c,
           fixture.input.getKeyQueue().empty() &&
             fixture.input.getCharQueue().empty(),
           "a switch drains the key and character queues");
  testTrue(c,
           !fixture.commands.HasCommand("play") &&
             fixture.commands.HasCommand("pause"),
           "a kept scene's commands are withdrawn");

  director.switchTo("menu");
  director.applyPending();
  testTrue(c,
           fixture.commands.HasCommand("play") &&
             !fixture.commands.HasCommand("pause"),
           "resuming registers them again");

  menu.allowClose = false;
  testTrue(c,
           !director.closeRequested(),
           "close negotiation goes to the active scene");
  director.stopAll();
  testTrue(c,
           !fixture.commands.HasCommand("play") && menu.stops == 1 &&
             game.stops == 1,
           "stopping withdraws commands and stops every started scene");
  director.stopAll();
  testTrue(c,
           menu.stops == 1 && game.stops == 1,
           "stopping twice changes nothing");
  return c.failures;
}

static int
testCameraPerScene()
{
  TestCounters c;
  DirectorFixture fixture;
  fixture.camera.SetZoom(2.0f);
  SceneProbe title;
  SceneProbe canvas;
  SceneDirector director(fixture.context);
  addProbe(director, "title", title);
  addProbe(director, "canvas", canvas);
  director.switchTo("title");
  director.applyPending();
  testTrue(c,
           fixture.camera.GetZoom() == 2.0f,
           "a new scene starts from the director's initial camera");
  fixture.camera.SetZoom(0.5f);
  fixture.camera.SetPosition(glm::vec2(10.0f, 20.0f));

  director.switchTo("canvas");
  director.applyPending();
  testTrue(c,
           fixture.camera.GetZoom() == 2.0f &&
             fixture.camera.GetPosition() == glm::vec2(0.0f, 0.0f),
           "the next new scene does not inherit the previous camera");
  fixture.camera.SetZoom(4.0f);

  director.switchTo("title");
  director.applyPending();
  testTrue(c,
           fixture.camera.GetZoom() == 0.5f &&
             fixture.camera.GetPosition() == glm::vec2(10.0f, 20.0f),
           "a resumed scene gets its own camera back");
  director.switchTo("canvas");
  director.applyPending();
  testTrue(c,
           fixture.camera.GetZoom() == 4.0f,
           "and so does the other one");
  return c.failures;
}

static int
testWorldsAndContent()
{
  TestCounters c;
  NullWorld shared;
  {
    DirectorFixture fixture;
    fixture.context.renderWorld = &shared;
    SceneProbe one;
    SceneProbe two;
    SceneDirector director(fixture.context);
    addProbe(director, "one", one);
    addProbe(director, "two", two);
    director.switchTo("one");
    director.applyPending();
    director.switchTo("two");
    director.applyPending();
    testTrue(c,
             one.worldAtStart == &shared && two.worldAtStart == &shared &&
               fixture.context.renderWorld == &shared,
             "without per-scene worlds every scene shares the context's");
    testTrue(c,
             director.find("one")->content().nodeCount() == 0u,
             "every started scene has (empty) content");
  }
  {
    DirectorFixture fixture;
    fixture.context.renderWorld = &shared;
    FakeWorlds worlds;
    SceneProbe one;
    SceneProbe two;
    {
      SceneDirector director(fixture.context, &worlds);
      addProbe(director, "one", one);
      addProbe(director, "two", two);
      director.switchTo("one");
      director.applyPending();
      IRenderWorld* first = director.find("one")->world();
      testTrue(c,
               worlds.created == 1 && first == one.worldAtStart &&
                 first != &shared && worlds.activeWorld == first &&
                 fixture.context.renderWorld == first,
               "a starting scene gets its own world, which becomes active");
      director.switchTo("two");
      director.applyPending();
      IRenderWorld* second = director.find("two")->world();
      testTrue(c,
               worlds.created == 2 && second != first &&
                 worlds.activeWorld == second &&
                 fixture.context.renderWorld == second,
               "each scene draws through its own world");
      director.switchTo("one");
      director.applyPending();
      testTrue(c,
               worlds.created == 2 && worlds.activeWorld == first &&
                 fixture.context.renderWorld == first,
               "resuming activates the kept world again");
      testTrue(c,
               director.release("two") && worlds.destroyed == 1 &&
                 worlds.destroyedWorlds.front() == second,
               "releasing a scene destroys its world");
    }
    testTrue(c,
             worlds.destroyed == 2,
             "stopping the director destroys the remaining worlds");
  }
  return c.failures;
}

static int
testStopOrder()
{
  TestCounters c;
  DirectorFixture fixture;
  std::vector<std::string> log;
  SceneProbe a;
  SceneProbe b;
  SceneProbe unused;
  a.log = &log;
  b.log = &log;
  unused.log = &log;
  {
    SceneDirector director(fixture.context);
    addProbe(director, "a", a);
    addProbe(director, "b", b);
    addProbe(director, "unused", unused);
    director.switchTo("b");
    director.applyPending();
    director.switchTo("a");
    director.applyPending();
    log.clear();
  }
  const std::vector<std::string> expected = { "a:leave",       "a:stop",
                                              "b:stop",        "unused:destroy",
                                              "b:destroy",     "a:destroy" };
  testTrue(c,
           log == expected,
           "shutdown leaves and stops the active scene, stops the kept ones "
           "newest first, and never stops a scene that did not start");
  return c.failures;
}

void
registerSceneDirectorTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Content.SceneLifecycle",
               []() { return testLifecycle(); });
  registry.add("Illumo.Content.SceneFailedStart",
               []() { return testFailedStart(); });
  registry.add("Illumo.Content.SceneCoverSwitch",
               []() { return testCoverSwitch(); });
  registry.add("Illumo.Content.SceneInputAndCommands",
               []() { return testFrameInputAndCommands(); });
  registry.add("Illumo.Content.SceneCamera",
               []() { return testCameraPerScene(); });
  registry.add("Illumo.Content.SceneWorlds",
               []() { return testWorldsAndContent(); });
  registry.add("Illumo.Content.SceneStopOrder",
               []() { return testStopOrder(); });
}
