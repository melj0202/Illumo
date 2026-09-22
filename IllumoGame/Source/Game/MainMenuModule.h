#pragma once

#include "ConfigurationMenu.h"
#include "NewSimulationMenu.h"
#include <Illumo/Engine/IModule.h>
#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
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

private:
  static const int kPlayItem = 0;
  static const int kLoadItem = 1;
  static const int kSettingsItem = 2;
  static const int kExitItem = 3;
  static const int kItemCount = 4;
  // The shell's modal reveal is tuned for overlays; the title screen enters
  // more slowly behind its own clock.
  static constexpr float kEntranceSeconds = 0.45f;
  static constexpr float kEntranceCeilingSeconds = 0.6f;
  static constexpr float kItemEntranceSeconds = 0.32f;
  static constexpr float kItemEntranceStaggerSeconds = 0.035f;

  void openCanvasSetup();
  void seedAmbientPattern();
  void advanceAmbientSimulation(double dt);
  void updateLayout();
  void rebuildVisual();
  void selectItem(int item);
  void activateSelectedItem();
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
  std::shared_ptr<Font> m_titleFont;
  int m_titleRasterSize = 0;
  int m_selectedItem;
  double m_bgSimAccum;

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
