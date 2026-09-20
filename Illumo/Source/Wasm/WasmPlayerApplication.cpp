#include <Illumo/Engine/Application.h>
#include <Illumo/Wasm/WasmGameModule.h>
#include <filesystem>
#include <fstream>

static std::vector<std::byte>
readModule(const std::string& path)
{
  if (path.empty()) {
    return {};
  }
  // User-selected module bytes only. No native artifacts or DLL search paths.
  std::ifstream input(
    std::filesystem::path(std::u8string(path.begin(), path.end())),
    std::ios::binary | std::ios::ate);
  const std::streamoff size = input.tellg();
  if (!input || size <= 0 || size > 64 * 1024 * 1024) {
    return {};
  }
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  input.seekg(0);
  if (!input.read(reinterpret_cast<char*>(bytes.data()), size)) {
    return {};
  }
  return bytes;
}

static std::unique_ptr<IModule>
createGuestModule(IEnvVars* environment)
{
  if (environment == nullptr) {
    return nullptr;
  }
  std::vector<std::byte> game =
    readModule(environment->getVar("GuestModule").value);
  const std::string modPath = environment->getVar("GuestMod").value;
  std::vector<std::byte> mod = readModule(modPath);
  const std::string workerPath = environment->getVar("GuestWorker").value;
  std::vector<std::byte> worker = readModule(workerPath);
  if (game.empty() || (!modPath.empty() && mod.empty()) ||
      (!workerPath.empty() && worker.empty())) {
    return nullptr;
  }
  return std::make_unique<WasmGameModule>(std::move(game),
                                          std::vector<std::byte>{},
                                          WasmLimits{},
                                          std::move(mod),
                                          std::move(worker));
}

IllumoApplicationDefinition
CreateIllumoApplication()
{
  IllumoApplicationDefinition application;
  application.applicationName = "Illumo WASM Player";
  application.commandLine.applicationName = application.applicationName;
  application.commandLine.description = "Isolated game reactor host";
  application.commandLine.usage =
    "IllumoWasmPlayer.exe --game path/to/game.wasm [--mod path/to/mod.wasm] "
    "[--worker path/to/worker.wasm]";
  application.commandLine.applicationOptions = {
    { "--game", "module", "GuestModule", "WASM game reactor to run" },
    { "--mod", "module", "GuestMod", "Isolated game-compatible mod reactor" },
    { "--worker", "module", "GuestWorker", "Isolated game compute reactor" }
  };
  application.createRequiredModule = createGuestModule;
  return application;
}
