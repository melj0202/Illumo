#pragma once

#include "ConfigurationMenu.h"
#include "MenuMotifs.h"
#include "NewSimulationMenu.h"
#include <Illumo/Engine/IModule.h>
#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <cstdint>
#include <memory>
#include <string>

class CanvasView;
class Font;
class CellContext;

class MainMenuModule : public IModule
{
public:
  MainMenuModule();
  ~MainMenuModule() override;

  MainMenuModule(const MainMenuModule&) = delete;
  MainMenuModule& operator=(const MainMenuModule&) = delete;
  MainMenuModule(MainMenuModule&&) = delete;
  MainMenuModule& operator=(MainMenuModule&&) = delete;

  bool Start(IllumoContext* context) override;
  void Update(double dt) override;
  void DispatchDrawables(Scene* scene) override;
  void Exit() override;

  int getSelectedItemForTesting() const { return m_selectedItem; }
  bool isSettingsOpenForTesting() const;
  bool isCanvasSetupOpenForTesting() const
  {
    return m_newSimulationMenu && m_newSimulationMenu->isOpen();
  }
  void selectItemForTesting(int item);
  void activateSelectedItemForTesting();
  const CellContext* ambientContextForTesting() const
  {
    return m_bgContext.get();
  }

private:
  static const int kPlayItem = 0;
  static const int kLoadItem = 1;
  static const int kSettingsItem = 2;
  static const int kExitItem = 3;
  static const int kItemCount = 4;
  // The shell's modal reveal is tuned for overlays; the title screen enters
  // more slowly behind its own clock, with the title letters cascading in.
  static constexpr float kEntranceSeconds = 0.45f;
  static constexpr float kEntranceCeilingSeconds = 1.4f;
  static constexpr float kItemEntranceSeconds = 0.32f;
  static constexpr float kItemEntranceStaggerSeconds = 0.035f;
  static constexpr float kTitleLetterDelaySeconds = 0.10f;
  static constexpr float kTitleLetterStaggerSeconds = 0.07f;
  static constexpr float kTitleLetterSeconds = 0.55f;
  // Background world: generations per second, the pause between visitors
  // (gliders and spaceships launched in from the edges), and the growth or
  // age at which the world is reseeded.
  static constexpr double kAmbientStepsPerSecond = 10.0;
  static constexpr float kVisitorSeconds = 4.5f;
  static constexpr std::size_t kReseedChunkCount = 1400;
  static constexpr double kReseedSeconds = 900.0;
  static constexpr float kAmbientZoom = 0.5f;

  void openCanvasSetup();
  void seedAmbientPattern();
  void advanceAmbientSimulation(double dt);
  void launchVisitor();
  void stampPattern(const char* name,
                    std::int64_t originX,
                    std::int64_t originY,
                    bool flipX,
                    bool flipY,
                    unsigned char state);
  std::uint32_t nextRandom();
  void updateMotion(float dt);
  void updateLayout();
  void rebuildVisual();
  void drawBackground(float width, float height, float reveal);
  void drawTitle(float room, unsigned char opacity);
  void drawRows(float room, unsigned char opacity, float breathe);
  void drawFooter(unsigned char opacity);
  void selectItem(int item);
  void activateSelectedItem();
  void pressItem(float originX, float originY);
  void registerConsoleCommands();
  void unregisterConsoleCommands();
  SimulatorConfiguration currentConfiguration() const;
  bool applyConfiguration(const SimulatorConfiguration& configuration);

  float itemPosition() const;

  std::unique_ptr<CellContext> m_bgContext;
  std::unique_ptr<ConfigurationMenu> m_configurationMenu;
  std::unique_ptr<NewSimulationMenu> m_newSimulationMenu;
  GameVisual m_menuVisual;
  GuiMenuAnimator m_animator;
  GuiPointerTracker m_pointer;
  GuiPanelFit m_panelFit;
  // Per-row hover/focus emphasis, the panel receding behind an overlay, and
  // pointer-driven parallax and spotlight.
  GuiSpringArray m_rowEmphasis;
  GuiSpring m_recede;
  GuiSpring m_parallaxX;
  GuiSpring m_parallaxY;
  GuiSpring m_spotX;
  GuiSpring m_spotY;
  GuiSpring m_spotStrength;
  CellMotif m_motif;
  float m_pressX = 0.0f;
  float m_pressY = 0.0f;
  std::shared_ptr<Font> m_titleFont;
  int m_titleRasterSize = 0;
  int m_selectedItem;
  double m_bgSimAccum;
  double m_worldElapsed = 0.0;
  float m_visitorElapsed = 0.0f;
  std::uint32_t m_randomState = 0x2545F491u;
  int m_visitorCount = 0;

  float m_panelX;
  float m_panelY;
  float m_panelWidth;
  float m_panelHeight;
  float m_firstItemY;
  float m_itemHeight;
  float m_itemWidth;
  float m_revealElapsed = 0.0f;
  // Platform completions hold a weak reference and are dropped after Exit.
  std::shared_ptr<bool> m_lifetime = std::make_shared<bool>(true);
  float entranceReveal() const;
  bool reducedMotion() const;
};
