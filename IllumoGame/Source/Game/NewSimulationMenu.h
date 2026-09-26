#pragma once

#include "MenuMotifs.h"
#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <array>
#include <cstdint>
#include <string>

class InputManager;
class DrawList;

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
  // The rows' boxes, labels and values and the footer.
  GameVisual& getVisual() { return layers[kContentLayer]; }
  // Adds every layer, back to front.
  void addDrawables(DrawList& scene);

private:
  static constexpr int kRowCount = 8;
  // Back to front. The glass; the header; the glider, the badge's glow and
  // its label; the cards; the selection drop; the rows' content and footer;
  // and the create row's breathing value. The animated layers stay small, so
  // the host re-records only those while the menu idles (D-R29).
  enum Layer : std::size_t
  {
    kGlassLayer,
    kHeaderLayer,
    kAccentLayer,
    kCardLayer,
    kDropLayer,
    kContentLayer,
    kPulseLayer,
    kLayerCount
  };  void rebuild();
  void drawRows(unsigned char opacity, float breathe);
  void drawFooter(float height, unsigned char opacity);
  void change(int direction);
  void select(int direction);
  void selectRow(int row);
  NewSimulationAction activate();
  IRenderWindow* window;
  Renderer* renderer;
  std::array<GameVisual, kLayerCount> layers;
  GuiMenuAnimator animator;
  GuiPointerTracker pointer;
  GuiPanelFit panelFit;
  NewSimulationConfiguration draft;
  bool finite = false;
  bool openState = false;
  int selected = 0;
  // Per-row hover/focus emphasis and the boundary badge's infinite/finite
  // crossfade, both spring-driven.
  GuiSpringArray focus;
  GuiSpring modeBlend;
  // The panel swivels toward the pointer; x and y carry the body shift, so
  // rows are hit where they are drawn.
  GuiPanelTilt tilt;
  CellMotif motif;
  float x = 0;
  float y = 0;
  float width = 0;
  float rowHeight = 0;
};
