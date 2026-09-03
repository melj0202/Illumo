#pragma once

#include <Illumo/Engine/IModuleHost.h>
#include <Illumo/Engine/IllumoContext.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

class AssetManager;
class Camera;
class CommandLine;
class CommandRegistry;
class EnvVars;
class IBackend;
class IEnvVars;
class IModule;
class InputManager;
class Renderer;
class Scene;

struct IllumoConfig
{
  std::string applicationName{ "Illumo" };
  std::string environmentPath;
};

class IllumoTestAccess;

enum class ModuleRequirement
{
  Optional,
  Required
};

class Illumo : public IModuleHost
{
public:
  explicit Illumo(IllumoConfig config = {});
  ~Illumo() override;

  Illumo(const Illumo&) = delete;
  Illumo& operator=(const Illumo&) = delete;
  Illumo(Illumo&&) = delete;
  Illumo& operator=(Illumo&&) = delete;

  EnvVars& environment();
  const EnvVars& environment() const;
  const std::string& applicationName() const;

  bool initialize();
  void addModule(std::unique_ptr<IModule> module,
                 ModuleRequirement requirement = ModuleRequirement::Optional);
  bool startModules();
  void update(double dt);
  void render();
  void shutdown() noexcept;

  void RequestTransition(std::unique_ptr<IModule> nextModule) override;
  bool HasPendingTransition() const override;

  IllumoContext& context();
  const IllumoContext& context() const;
  bool shouldClose() const;

private:
  friend class IllumoTestAccess;

  using WindowFactory = std::function<
    std::unique_ptr<IRenderWindow>(int, int, const std::string&, IEnvVars*)>;
  using BackendFactory =
    std::function<std::unique_ptr<IBackend>(IRenderWindow*)>;

  struct RegisteredModule
  {
    std::unique_ptr<IModule> module;
    ModuleRequirement requirement{ ModuleRequirement::Optional };
    bool started{ false };
  };

  void applyHostDefaults();
  void clearContext();
  void stopModule(RegisteredModule& registration, bool force) noexcept;
  void rollbackStartedModules() noexcept;
  void applyPendingModuleTransition();
  void updateStartedModules(ModuleRequirement requirement, double dt);
  void dispatchStartedModules(ModuleRequirement requirement);
  void processGlobalHotkeys();
  void releaseServices();

  std::string m_applicationName;
  WindowFactory m_windowFactory;
  BackendFactory m_backendFactory;
  std::unique_ptr<EnvVars> m_environment;
  std::unique_ptr<Camera> m_camera;
  std::unique_ptr<IRenderWindow> m_window;
  std::unique_ptr<Renderer> m_renderer;
  std::unique_ptr<AssetManager> m_assetManager;
  std::unique_ptr<CommandRegistry> m_commandRegistry;
  std::unique_ptr<CommandLine> m_commandLine;
  std::unique_ptr<InputManager> m_inputManager;
  std::unique_ptr<Scene> m_scene;
  IllumoContext m_context{};
  std::vector<RegisteredModule> m_modules;
  std::unique_ptr<IModule> m_pendingModuleTransition;
  bool m_initialized{ false };
  bool m_modulesStarted{ false };
};
