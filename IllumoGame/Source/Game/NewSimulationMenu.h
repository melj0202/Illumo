#pragma once

#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <array>
#include <cstdint>
#include <string>

class InputManager;

// Startup-only canvas choices; deliberately independent of display preferences.
struct NewSimulationConfiguration
{
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
  void rebuild();
  void change(int direction);
  void select(int direction);
  NewSimulationAction activate();
  IRenderWindow* window;
  Renderer* renderer;
  GameVisual visual{ 4096u };
  NewSimulationConfiguration draft;
  bool finite = false;
  bool openState = false;
  bool reduced = false;
  bool mouseWasDown = true;
  int selected = 0;
  float elapsed = 0;
  float ambientPhase = 0;
  float valuePulse = 0;
  std::array<float, 7> focus{};
  float scale = 1;
  float x = 0;
  float y = 0;
  float width = 0;
  float rowHeight = 0;
  float previousX = -1;
  float previousY = -1;
};
