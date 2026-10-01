#pragma once

#include <Illumo/Content/BehaviourSchema.h>
#include <Illumo/Content/ProgramScene.h>
#include <Illumo/Content/SceneBehaviours.h>
#include <IllumoGuest/Documents.h>
#include <IllumoGuest/FileTree.h>
#include <IllumoGuest/Program.h>
#include <IllumoGuest/SceneFetches.h>
#include <memory>
#include <string>

// A game that plays .ilsc scenes with scene behaviours
// (docs/scene-behaviours-design.md). It plays the document launched with
// --open (how IllEd's Play starts it), else its packaged default scene. The
// package's /app/behaviours.json describes the behaviours the product
// registers in registerBehaviours(); the two are compared at startup and any
// difference is logged.
//
// A launched scene's package-relative asset references resolve against the
// "root" member of its "illumo.play" extension (IllEd writes the edited
// document's package root there), else /local. A packaged scene resolves
// against its own package.
class GuestPlayProgram : public GuestProgram
{
public:
  // `defaultScene`: a virtual path ("/app/Scenes/demo.ilsc") played when no
  // document was launched; empty plays nothing.
  GuestPlayProgram(std::string applicationName, std::string defaultScene);
  ~GuestPlayProgram() override;
  GuestPlayProgram(const GuestPlayProgram&) = delete;
  GuestPlayProgram& operator=(const GuestPlayProgram&) = delete;
  GuestPlayProgram(GuestPlayProgram&&) = delete;
  GuestPlayProgram& operator=(GuestPlayProgram&&) = delete;

  const BehaviourRegistry& behaviourRegistry() const { return m_registry; }
  const BehaviourSchema& behaviourSchema() const { return m_schema; }
  GuestDocuments& documents() { return m_documents; }
  GuestFileTree& tree() { return m_tree; }
  GuestSceneFetches& fetches() { return m_fetches; }
  // The launched document, empty when none.
  const GuestDocumentLocation& launchDocument() const { return m_launch; }
  const std::string& defaultScene() const { return m_defaultScene; }

protected:
  // Registers the product's behaviour types; called once during bootstrap.
  virtual void registerBehaviours(BehaviourRegistry& registry) = 0;
  bool acceptStartup(std::span<const std::byte> startup) override;
  bool bootstrap() override;
  bool createScenes(SceneDirector& scenes) override;
  void pumpProduct() override;

private:
  BehaviourRegistry m_registry;
  BehaviourSchema m_schema;
  GuestDocuments m_documents;
  GuestFileTree m_tree;
  GuestSceneFetches m_fetches;
  GuestDocumentLocation m_launch;
  std::string m_defaultScene;
  bool m_schemaRequested = false;
  bool m_schemaSettled = false;
};

// The scene GuestPlayProgram plays: loads the document (collect, fetch,
// instantiate), runs its behaviours every frame, and views it through its
// primary camera node, else a default view of the origin. Registers
// play_node <id> (logs a node's position) and play_status for scripts and
// tests.
class GuestPlayScene final : public ProgramScene
{
public:
  explicit GuestPlayScene(GuestPlayProgram& program);
  ~GuestPlayScene() override;
  GuestPlayScene(const GuestPlayScene&) = delete;
  GuestPlayScene& operator=(const GuestPlayScene&) = delete;
  GuestPlayScene(GuestPlayScene&&) = delete;
  GuestPlayScene& operator=(GuestPlayScene&&) = delete;

  bool start(IllumoContext& context) override;
  void enter() override;
  void update(double elapsed) override;
  void dispatch(DrawList& frame) override;
  void stop() override;

  bool loaded() const { return m_loaded; }
  SceneBehaviours& behaviours() { return m_behaviours; }

private:
  GuestPlayProgram& m_program;
  SceneBehaviours m_behaviours;
  bool m_loaded = false;
  // Expires on stop so late file completions never touch a stopped scene.
  std::shared_ptr<bool> m_alive;

  void load();
  void read(
    std::function<void(bool, const std::string&, const std::string&)> done);
  void applyView();
};
