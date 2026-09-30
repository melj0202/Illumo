#pragma once

#include <Illumo/Engine/Illumo.h>

class IllumoTestAccess
{
public:
  using PlainWindowFactory = std::function<
    std::unique_ptr<IRenderWindow>(int, int, const std::string&, IEnvVars*)>;
  using PlainBackendFactory =
    std::function<std::unique_ptr<IBackend>(IRenderWindow*)>;

  // Factories that serve every graphics API the host tries.
  static void setWindowFactory(Illumo& host, PlainWindowFactory factory)
  {
    host.m_windowFactory = [factory](int width,
                                     int height,
                                     const std::string& title,
                                     IEnvVars* environment,
                                     BackendDef) {
      return factory(width, height, title, environment);
    };
  }

  static void setBackendFactory(Illumo& host, PlainBackendFactory factory)
  {
    host.m_backendFactory = [factory](IRenderWindow* window, BackendDef) {
      return factory(window);
    };
  }

  // Factories that see which API is being started.
  static void setGraphicsApiFactories(Illumo& host,
                                      Illumo::WindowFactory windowFactory,
                                      Illumo::BackendFactory backendFactory)
  {
    host.m_windowFactory = std::move(windowFactory);
    host.m_backendFactory = std::move(backendFactory);
  }

  static DrawList* getScene(Illumo& host) { return host.m_scene.get(); }

  static EnvVars* getEnvironment(Illumo& host)
  {
    return host.m_environment.get();
  }

  static void configureScenePipeline(Illumo& host)
  {
    host.configureScenePipeline();
  }
};
