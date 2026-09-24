#pragma once

#include "ConfigurationMenu.h"
#include "MenuMotifs.h"
#include "NewSimulationMenu.h"
#include <Illumo/Engine/IModule.h>
#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Rendering/FontWeightRamp.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <array>
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
  float tiltXForTesting() const { return m_tilt.x(); }
  float tiltYForTesting() const { return m_tilt.y(); }
  // The rectangle the title word was last drawn in: x, y, width, height.
  std::array<float, 4> titleBoundsForTesting() const { return m_titleBounds; }
  // Window pixels per virtual layout pixel.
  float layoutScaleForTesting() const { return m_panelFit.layoutScale; }
  // The rectangle a pointer must hit for `item`: x, y, width, height.
  std::array<float, 4> itemHitBoundsForTesting(int item) const;
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
  static constexpr float kEntranceCeilingSeconds = 1.8f;
  static constexpr float kItemEntranceSeconds = 0.32f;
  static constexpr float kItemEntranceStaggerSeconds = 0.035f;
  static constexpr float kTitleLetterDelaySeconds = 0.10f;
  static constexpr float kTitleLetterStaggerSeconds = 0.07f;
  static constexpr float kTitleLetterSeconds = 0.55f;
  // Title letters land like drops: a bouncy spring that settles well inside
  // the entrance ceiling.
  static constexpr float kTitleLetterBounceHz = 2.6f;
  static constexpr float kTitleLetterBounceDamping = 0.4f;
  // Title weight: letters fall thin and land at rest weight, the impact
  // squashing them heavier; then a swell of weight rolls through the word
  // with the bob, and letters near the pointer pool heavier.
  static constexpr float kTitleFallWeight = 220.0f;
  static constexpr float kTitleBreathWeight = 110.0f;
  static constexpr float kTitlePointerWeight = 300.0f;
  // Once the word has landed, one letter at a time strikes a pose: it flexes
  // heavy and squat, slims thin and tall, or hops (thin in flight, squashed
  // heavy on landing), holds it briefly and springs back; now and then a wave
  // of hops ripples out from it. The next pose comes after a random pause;
  // clicking the word sends a hop through it.
  static constexpr int kTitleLetterCount = 4;
  static constexpr float kTitlePoseStartSeconds = 1.1f;
  static constexpr float kTitlePoseMinPauseSeconds = 0.55f;
  static constexpr float kTitlePosePauseRangeSeconds = 1.0f;
  static constexpr float kTitleHopStaggerSeconds = 0.08f;
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
  void updateTitlePoses(float dt);
  void strikeTitlePose(int letter);
  void hopTitleLetter(int letter);
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

  std::unique_ptr<CellContext> m_bgContext;
  std::unique_ptr<ConfigurationMenu> m_configurationMenu;
  std::unique_ptr<NewSimulationMenu> m_newSimulationMenu;
  GameVisual m_menuVisual;
  GuiMenuAnimator m_animator;
  GuiPointerTracker m_pointer;
  GuiPanelFit m_panelFit;
  // Per-row hover/focus emphasis, the panel receding behind an overlay, the
  // panel's tilt toward the pointer, and pointer-driven parallax and
  // spotlight.
  GuiSpringArray m_rowEmphasis;
  GuiSpring m_recede;
  // The panel swivels toward the pointer; rows are hit where they are drawn.
  GuiPanelTilt m_tilt;
  GuiSpring m_parallaxX;
  GuiSpring m_parallaxY;
  GuiSpring m_spotX;
  GuiSpring m_spotY;
  GuiSpring m_spotStrength;
  CellMotif m_motif;
  float m_pressX = 0.0f;
  float m_pressY = 0.0f;
  std::shared_ptr<Font> m_titleFont;
  // The title's weights when the variable typeface is installed (then
  // m_titleFont is unused); empty otherwise, and the title draws in
  // m_titleFont at one weight.
  FontWeightRamp m_titleRamp;
  int m_titleRasterSize = 0;
  // A title letter's pose: extra weight, squash (+ squat and wide, - tall and
  // narrow) and hop height, each on its own spring, plus the time left
  // holding the pose and a pending hop from a click.
  struct TitleLetterPose
  {
    GuiSpring weight;
    GuiSpring squash;
    GuiSpring hop;
    float hold = 0.0f;
    float hopDelay = -1.0f;
  };
  std::array<TitleLetterPose, kTitleLetterCount> m_letterPoses;
  float m_poseCountdown = 0.0f;
  int m_lastPosedLetter = -1;
  // Where the word was last drawn (x, y, width, height), for clicks on it.
  std::array<float, 4> m_titleBounds{};
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
