#pragma once

#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <array>
#include <cstdint>
#include <string>

class InputManager;

// Startup-only canvas choices; deliberately independent of display preferences.
struct NewSimulationConfiguration
{
  std::string family = "LIFE_LIKE_BINARY";
  std::string ruleSet = "GAME_OF_LIFE";
  std::int64_t worldChunkWidth = 0;
  std::int64_t worldChunkHeight = 0;
  bool starterPattern = true;
  bool isValid() const;
};

enum class NewSimulationAction
{
  None,
  Create,
  Back
};

class NewSimulationMenu
{
public:
  NewSimulationMenu(IRenderWindow* window, Renderer* renderer);
  NewSimulationMenu(const NewSimulationMenu&) = delete;
  NewSimulationMenu& operator=(const NewSimulationMenu&) = delete;
  void open(const NewSimulationConfiguration& initial, bool reducedMotion);
  void close() { openState = false; }
  bool isOpen() const { return openState; }
  void tick(float dt);
  NewSimulationAction update(InputManager* input);
  NewSimulationConfiguration configuration() const;
  GameVisual& getVisual() { return visual; }

private:
  static constexpr int kRowCount = 8;
  void rebuild();
  void change(int direction);
  void select(int direction);
  NewSimulationAction activate();
  IRenderWindow* window;
  Renderer* renderer;
  GameVisual visual{ 4096u };
  GuiMenuAnimator animator;
  GuiPointerTracker pointer;
  GuiPanelFit panelFit;
  NewSimulationConfiguration draft;
  bool finite = false;
  bool openState = false;
  int selected = 0;
  // Per-row emphasis; this menu lifts every row rather than gliding one
  // highlight, so it keeps its own focus weights alongside the shared clocks.
  std::array<float, kRowCount> focus{};
  float x = 0;
  float y = 0;
  float width = 0;
  float rowHeight = 0;
};
