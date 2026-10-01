#include "PlaygroundBehaviours.h"
#include <IllumoGuest/PlayProgram.h>
#include <memory>

// The Illumo Playground: the sample game for scene behaviours
// (docs/scene-behaviours-design.md). It plays the scene IllEd's Play sends it
// (--open), else its own Scenes/demo.ilsc, and runs the sample behaviours on
// it. A real game starts from a copy of this file and PlaygroundBehaviours.
class PlaygroundGuest final : public GuestPlayProgram
{
public:
  PlaygroundGuest()
    : GuestPlayProgram("Illumo Playground", "/app/Scenes/demo.ilsc")
  {
  }

  GuestDescriptor describe() const override
  {
    return { GuestRole::Game,
             static_cast<std::uint32_t>(GuestCapability::Render) |
               static_cast<std::uint32_t>(GuestCapability::Assets) |
               static_cast<std::uint32_t>(GuestCapability::Storage) |
               static_cast<std::uint32_t>(GuestCapability::SelectedFiles) |
               static_cast<std::uint32_t>(GuestCapability::Console) |
               static_cast<std::uint32_t>(GuestCapability::Display),
             "playground",
             {} };
  }

protected:
  void registerBehaviours(BehaviourRegistry& registry) override
  {
    registerPlaygroundBehaviours(registry);
  }
};

std::unique_ptr<GuestApplication>
CreateGuestApplication()
{
  return std::make_unique<PlaygroundGuest>();
}
