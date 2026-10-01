#include <IllumoGuest/PlayProgram.h>

#include <Illumo/Content/IlscCodec.h>
#include <Illumo/Content/SceneAssetRefs.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/Logger.h>
#include <cstdio>
#include <nlohmann/json.hpp>
#include <utility>

// ---------------------------------------------------------------------------
// Program.

GuestPlayProgram::GuestPlayProgram(std::string applicationName,
                                   std::string defaultScene)
  : GuestProgram(std::move(applicationName))
  , m_documents(services(), files())
  , m_tree(files())
  , m_fetches(assetCache())
  , m_defaultScene(std::move(defaultScene))
{
}

GuestPlayProgram::~GuestPlayProgram() = default;

bool
GuestPlayProgram::acceptStartup(std::span<const std::byte> startup)
{
  if (!GuestProgram::acceptStartup(startup)) {
    return false;
  }
  if (launchFile() != nullptr) {
    m_launch = GuestDocuments::launch(*launchFile());
  }
  return true;
}

void
GuestPlayProgram::pumpProduct()
{
  m_documents.pump();
  m_tree.pump();
  m_fetches.pump();
}

bool
GuestPlayProgram::bootstrap()
{
  if (!m_schemaRequested) {
    m_schemaRequested = true;
    const std::string path = std::string("/app/") + BehaviourSchema::kFileName;
    const bool queued = m_tree.read(
      path,
      [this](GuestFileOutcome outcome, std::vector<std::byte> bytes) {
        if (outcome == GuestFileOutcome::Success) {
          std::string text(bytes.size(), '\0');
          for (std::size_t index = 0; index < bytes.size(); ++index) {
            text[index] = static_cast<char>(bytes[index]);
          }
          std::string error;
          if (!m_schema.parse(text, error)) {
            Logger::LogWarning("behaviours.json was refused: " + error);
          }
        } else {
          Logger::LogTrace("The package has no behaviours.json");
        }
        registerBehaviours(m_registry);
        for (const std::string& message : m_registry.compare(m_schema)) {
          Logger::LogWarning(message);
        }
        Logger::LogTrace(std::to_string(m_registry.types().size()) +
                         " behaviour type(s) registered");
        m_schemaSettled = true;
      },
      BehaviourSchema::kMaximumFileBytes);
    if (!queued) {
      registerBehaviours(m_registry);
      m_schemaSettled = true;
    }
  }
  return m_schemaSettled && GuestProgram::bootstrap();
}

bool
GuestPlayProgram::createScenes(SceneDirector& scenes)
{
  scenes.emplace<GuestPlayScene>("play", *this);
  return scenes.switchTo("play");
}

// ---------------------------------------------------------------------------
// Scene.

GuestPlayScene::GuestPlayScene(GuestPlayProgram& program)
  : m_program(program)
  , m_behaviours(program.behaviourRegistry(), program.behaviourSchema())
{
}

GuestPlayScene::~GuestPlayScene() = default;

bool
GuestPlayScene::start(IllumoContext& startContext)
{
  m_alive = std::make_shared<bool>(true);
  m_behaviours.setInput(startContext.inputManager);
  load();
  return true;
}

// The package root a launched scene's relative references resolve against:
// its "illumo.play" extension's "root", else /local.
static std::string
launchedRoot(const SceneDocument& document)
{
  for (const SceneExtension& extension : document.extensions) {
    if (extension.key != "illumo.play") {
      continue;
    }
    const nlohmann::json data =
      nlohmann::json::parse(extension.data, nullptr, false);
    if (data.is_object() && data.contains("root") && data["root"].is_string()) {
      const std::string root = data["root"].get<std::string>();
      std::string normalized;
      if (!root.empty() && root.front() == '/' &&
          scenePackageRoot(root + "/scene.ilsc", normalized) &&
          normalized == root) {
        return root;
      }
    }
  }
  return "/local";
}

void
GuestPlayScene::read(
  std::function<void(bool, const std::string&, const std::string&)> done)
{
  const GuestDocumentLocation& launch = m_program.launchDocument();
  if (!launch.empty()) {
    m_program.documents().read(launch.location, std::move(done));
    return;
  }
  const bool queued = m_program.tree().read(
    m_program.defaultScene(),
    [done](GuestFileOutcome outcome, std::vector<std::byte> bytes) {
      std::string text(bytes.size(), '\0');
      for (std::size_t index = 0; index < bytes.size(); ++index) {
        text[index] = static_cast<char>(bytes[index]);
      }
      const bool success = outcome == GuestFileOutcome::Success;
      done(success, text, success ? std::string() : "cannot read the scene");
    });
  if (!queued) {
    done(false, {}, "too many file operations in flight");
  }
}

void
GuestPlayScene::load()
{
  const GuestDocumentLocation& launch = m_program.launchDocument();
  if (launch.empty() && m_program.defaultScene().empty()) {
    Logger::LogWarning("Nothing to play: no scene was opened");
    return;
  }
  const std::string name =
    launch.empty() ? m_program.defaultScene() : launch.label;
  const std::weak_ptr<bool> alive = m_alive;
  read([this, alive, name](
         bool success, const std::string& text, const std::string& readError) {
    if (alive.expired()) {
      return;
    }
    SceneDocument parsed;
    std::string error = readError;
    if (!success || !IlscCodec::parse(text, parsed, error)) {
      Logger::LogError("Cannot play " + name + ": " + error);
      return;
    }
    std::string root;
    if (!m_program.launchDocument().empty()) {
      root = launchedRoot(parsed);
    } else if (!scenePackageRoot(m_program.defaultScene(), root)) {
      root = "/app";
    }
    // Collect the references and fetch them before the scene instantiates.
    const SceneFetchList fetches = collectSceneFetches(parsed, root);
    m_program.fetches().fetch(
      fetches.paths,
      [this, alive, parsed, name, root, unresolved = fetches.unresolved](
        std::vector<std::string> missing) {
        if (alive.expired()) {
          return;
        }
        missing.insert(missing.end(), unresolved.begin(), unresolved.end());
        for (const std::string& path : missing) {
          Logger::LogWarning("Scene asset is missing: " + path);
        }
        std::string loadError;
        if (!content().load(parsed, root, loadError)) {
          Logger::LogError("Cannot play " + name + ": " + loadError);
          return;
        }
        m_behaviours.attach(content());
        m_loaded = true;
        applyView();
        Logger::LogInfo("Playing " + name + " (" +
                        std::to_string(content().nodeCount()) + " nodes, " +
                        std::to_string(m_behaviours.count()) +
                        " behaviours, root " + root + ")");
      });
  });
}

void
GuestPlayScene::enter()
{
  command(
    "play_node",
    [this](const std::vector<std::string>& args) {
      if (args.empty() || content().findNode(args[0]) == nullptr) {
        context().commandLine->logError("play_node: no such node");
        return;
      }
      const Matrix4 world = content().worldMatrix(args[0]);
      char line[160];
      std::snprintf(line,
                    sizeof(line),
                    "play_node %s %.4f %.4f %.4f",
                    args[0].c_str(),
                    static_cast<double>(world[3].x),
                    static_cast<double>(world[3].y),
                    static_cast<double>(world[3].z));
      context().commandLine->logNormal(line);
    },
    "play_node <id>",
    "Print a played node's world position");
  command(
    "play_status",
    [this](const std::vector<std::string>&) {
      context().commandLine->logNormal(
        std::string("play_status ") + (m_loaded ? "playing " : "loading ") +
        std::to_string(content().nodeCount()) + " nodes " +
        std::to_string(m_behaviours.count()) + " behaviours");
    },
    "play_status",
    "Print whether the scene is playing and how many behaviours run");
}

void
GuestPlayScene::applyView()
{
  Camera* camera = context().camera;
  if (camera == nullptr) {
    return;
  }
  std::string id;
  if (content().primaryCameraId(&id) && content().applyCamera(id, *camera)) {
    return;
  }
  if (content().document().worldMode == SceneWorldMode::World3D) {
    camera->lookAt(glm::vec3(6.0f, 5.0f, 9.0f),
                   glm::vec3(0.0f),
                   glm::vec3(0.0f, 1.0f, 0.0f));
    camera->setPerspective(50.0f, 0.1f, 250.0f);
    camera->setProjectionType(ProjectionType::Perspective);
  } else {
    camera->setProjectionType(ProjectionType::Orthographic);
    camera->SetPositionPrecise(0.0, 0.0);
    camera->SetZoom(32.0f);
  }
}

void
GuestPlayScene::update(double elapsed)
{
  if (!m_loaded) {
    return;
  }
  m_behaviours.update(elapsed);
  // Warnings are logged where they arise; drop the copies.
  m_behaviours.takeWarnings();
  // A camera node may move (a behaviour, or a parent that does).
  std::string id;
  if (context().camera != nullptr && content().primaryCameraId(&id)) {
    content().applyCamera(id, *context().camera);
  }
}

void
GuestPlayScene::dispatch(DrawList& frame)
{
  dispatchContent(frame);
}

void
GuestPlayScene::stop()
{
  m_behaviours.detach();
  m_loaded = false;
  m_alive.reset();
  m_program.fetches().release();
}
