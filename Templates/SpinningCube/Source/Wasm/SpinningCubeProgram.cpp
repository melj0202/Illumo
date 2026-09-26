#include "SpinningCubeConfig.h"
#include "SpinningCubeScene.h"

#include <IllumoGuest/Program.h>

// @PROJECT_NAME@ as a WASM program (D-E31). IllumoRuntime hosts it from
// apps/@PROJECT_ID@; the program lives as long as its WASM store and owns what
// every scene shares, and its SceneDirector runs the scenes.
class SpinningCubeProgram final : public GuestProgram
{
public:
  SpinningCubeProgram()
    : GuestProgram(SpinningCubeConfig::applicationName())
  {
  }

  GuestDescriptor describe() const override
  {
    return { GuestRole::Game,
             static_cast<std::uint32_t>(GuestCapability::Render) |
               static_cast<std::uint32_t>(GuestCapability::Storage) |
               static_cast<std::uint32_t>(GuestCapability::Console) |
               static_cast<std::uint32_t>(GuestCapability::Display),
             "@PROJECT_ID@",
             {} };
  }

protected:
  void applyDefaults(IEnvVars& settings) override
  {
    SpinningCubeConfig::ApplyDefaults(&settings);
  }
  // Add every scene here and switch to the first; a program with a title
  // screen would add it too and switch between them.
  bool createScenes(SceneDirector& scenes) override
  {
    scenes.emplace<SpinningCubeScene>("cube");
    return scenes.switchTo("cube");
  }
};

std::unique_ptr<GuestApplication>
CreateGuestApplication()
{
  return std::make_unique<SpinningCubeProgram>();
}
