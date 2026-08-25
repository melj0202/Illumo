#pragma once

#include "ConfigurationMenu.h"
#include <Illumo/Engine/IModule.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <memory>
#include <string>

class CanvasView;
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
  void selectItemForTesting(int item);
  void activateSelectedItemForTesting();

private:
  static const int kPlayItem = 0;
  static const int kLoadItem = 1;
  static const int kSettingsItem = 2;
  static const int kExitItem = 3;
  static const int kItemCount = 4;
  static constexpr float kSelectionAnimationSeconds = 0.14f;

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
  GameVisual m_menuVisual;
  int m_selectedItem;
  float m_animationElapsed;
  float m_selectionFromItem;
  float m_selectionAnimationElapsed;
  double m_bgSimAccum;
  bool m_mouseWasDown;

  float m_panelX;
  float m_panelY;
  float m_panelWidth;
  float m_panelHeight;
  float m_firstItemY;
  float m_itemHeight;
  float m_itemWidth;
};
