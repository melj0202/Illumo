#pragma once

#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>

class InputManager;
class IRenderWindow;
class Renderer;

enum class ExitConfirmAction
{
  None,
  Confirm,
  Cancel
};

// Primitive-composed exit confirmation overlay. This is not a retained widget
// tree: one GameVisual rebuilds chrome and the two actions each frame.
class ExitConfirmDialog : public DrawableBase
{
public:
  ExitConfirmDialog(IRenderWindow* window, Renderer* renderer);
  ~ExitConfirmDialog() override = default;

  ExitConfirmDialog(const ExitConfirmDialog&) = delete;
  ExitConfirmDialog& operator=(const ExitConfirmDialog&) = delete;

  void open();
  void close();
  bool isOpen() const { return openState; }
  void tick(float deltaSeconds);
  ExitConfirmAction update(InputManager* inputManager);
  GameVisual& getVisual() { return visual; }

  int getSelectedButtonForTesting() const { return selectedButton; }
  float getAnimationProgressForTesting() const;
  float getSelectionPositionForTesting() const;

  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;

private:
  static const int kCancelButton = 0;
  static const int kExitButton = 1;
  static const int kButtonCount = 2;
  static constexpr float kOpenAnimationSeconds = 0.36f;
  static constexpr float kSelectionAnimationSeconds = 0.14f;

  IRenderWindow* window;
  Renderer* renderer;
  GameVisual visual;
  bool openState;
  bool mouseWasDown;
  int selectedButton;
  float animationElapsed;
  float selectionFromButton;
  float selectionAnimationElapsed;
  float panelX;
  float panelY;
  float panelWidth;
  float panelHeight;
  float buttonY;
  float buttonWidth;
  float buttonHeight;
  float cancelX;
  float exitX;

  void updateLayout();
  void rebuildVisual();
  void selectButton(int button);
  float animationProgress() const;
  float panelReveal() const;
  float panelOffsetY() const;
  float itemReveal(int item) const;
  float selectionButtonPosition() const;
  ExitConfirmAction activateSelected() const;
  ExitConfirmAction clickAt(float x, float y);
};
