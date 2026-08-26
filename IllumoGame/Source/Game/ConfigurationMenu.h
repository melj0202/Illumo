#pragma once

#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <cstdint>
#include <string>

class InputManager;
class IRenderWindow;
class Renderer;

struct SimulatorConfiguration
{
  std::string ruleSet = "GAME_OF_LIFE";
  std::int64_t worldChunkWidth = 0;
  std::int64_t worldChunkHeight = 0;
  long tps = 12;
  double speedFactor = 1.0;
  double fadeSpeed = 6.0;
  bool vsync = true;
  bool fullscreen = false;
  long uiScale = 1;
  long msaa = 4;
};

enum class ConfigurationMenuAction
{
  None,
  Apply,
  Cancel,
  Exit
};

// Release-visible, primitive-composed settings overlay. This owns one visual
// and a draft value; it is deliberately not a retained widget hierarchy.
class ConfigurationMenu : public DrawableBase
{
public:
  ConfigurationMenu(IRenderWindow* window, Renderer* renderer);
  ~ConfigurationMenu() override = default;

  ConfigurationMenu(const ConfigurationMenu&) = delete;
  ConfigurationMenu& operator=(const ConfigurationMenu&) = delete;

  void open(const SimulatorConfiguration& current);
  void close();
  bool isOpen() const { return openState; }
  void tick(float deltaSeconds);
  ConfigurationMenuAction update(InputManager* inputManager);
  bool readConfiguration(SimulatorConfiguration* configuration,
                         std::string* error) const;
  void setError(const std::string& message);
  GameVisual& getVisual() { return visual; }

  int getSelectedRowForTesting() const { return selectedRow; }
  float getAnimationProgressForTesting() const;
  float getSelectionPositionForTesting() const;
  float getValuePulseForTesting() const;
  const std::string& getWorldWidthTextForTesting() const
  {
    return worldWidthText;
  }

  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;

private:
  static const int kRulesetRow = 0;
  static const int kWorldWidthRow = 1;
  static const int kWorldHeightRow = 2;
  static const int kTpsRow = 3;
  static const int kSpeedRow = 4;
  static const int kFadeRow = 5;
  static const int kVsyncRow = 6;
  static const int kFullscreenRow = 7;
  static const int kUiScaleRow = 8;
  static const int kMsaaRow = 9;
  static const int kApplyRow = 10;
  static const int kCancelRow = 11;
  static const int kExitRow = 12;
  static const int kRowCount = 13;
  static constexpr float kOpenAnimationSeconds = 0.36f;
  static constexpr float kSelectionAnimationSeconds = 0.14f;
  static constexpr float kValuePulseSeconds = 0.20f;

  IRenderWindow* window;
  Renderer* renderer;
  GameVisual visual;
  bool openState;
  bool mouseWasDown;
  bool replaceFieldOnType;
  int selectedRow;
  float animationElapsed;
  float selectionFromRow;
  float selectionAnimationElapsed;
  float valuePulseElapsed;
  float panelX;
  float panelY;
  float panelWidth;
  float panelHeight;
  float firstRowY;
  float rowHeight;

  std::string ruleSet;
  std::string worldWidthText;
  std::string worldHeightText;
  std::string tpsText;
  std::string speedText;
  std::string fadeText;
  bool vsync;
  bool fullscreen;
  long uiScale;
  long msaa;
  std::string errorMessage;

  void updateLayout();
  void rebuildVisual();
  float animationProgress() const;
  float panelReveal() const;
  float panelOffsetY() const;
  float rowReveal(int row) const;
  float selectionRowPosition() const;
  float valuePulse() const;
  void triggerValuePulse();
  void selectRow(int row);
  void cycleSelected(int direction);
  ConfigurationMenuAction activateSelected();
  void addCharacter(unsigned int codepoint);
  void eraseCharacter();
  std::string* editableField();
  static std::string topologyText(std::int64_t chunks);
  static std::string decimalText(double value);
  static std::string displayRuleSetName(const std::string& mode);
};
