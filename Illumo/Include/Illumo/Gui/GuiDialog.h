#pragma once

#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Gui/GuiTypes.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <string>
#include <vector>

class InputManager;
class IRenderWindow;
class Renderer;

// Reusable modal confirmation / alert dialog component.
// Composes GameVisual primitives to render an animated backdrop, panel chrome,
// message, and action button bar with keyboard navigation and mouse hit
// testing.
class GuiDialog : public DrawableBase
{
public:
  static constexpr float kDefaultFontSize = 13.0f;
  static constexpr float kDefaultPanelWidth = 420.0f;
  static constexpr float kDefaultPanelHeight = 160.0f;
  static constexpr float kOpenAnimationSeconds = 0.24f;
  static constexpr float kSelectionAnimationSeconds = 0.14f;

  GuiDialog(IRenderWindow* window, Renderer* renderer);
  ~GuiDialog() override = default;

  GuiDialog(const GuiDialog&) = delete;
  GuiDialog& operator=(const GuiDialog&) = delete;

  void setTitle(const std::string& title);
  const std::string& getTitle() const { return m_title; }

  void setMessage(const std::string& message);
  const std::string& getMessage() const { return m_message; }

  void addButton(const GuiButtonDef& button);
  void clearButtons();
  size_t buttonCount() const { return m_buttons.size(); }
  const GuiButtonDef& getButton(size_t index) const { return m_buttons[index]; }

  void open();
  void open(const std::string& message);
  void close();
  bool isOpen() const { return m_open; }

  void setFontSize(float sizePt);
  float fontSize() const { return m_fontSize; }

  void setPanelDimensions(float width, float height);
  float panelWidth() const { return m_panelWidth; }
  float panelHeight() const { return m_panelHeight; }

  void tick(float dt);
  int update(InputManager* inputManager, float dt = 0.016f);
  int clickAt(float x, float y);

  void selectButton(int buttonIndex);
  int selectedButton() const { return m_selectedButton; }
  int hoveredButton() const { return m_hoveredButton; }

  float animationProgress() const;
  float panelOffsetY() const;
  float selectionPosition() const;

  GameVisual& getVisual() { return m_visual; }

  void updateLayout();
  void rebuildVisual();

  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;

private:
  IRenderWindow* m_window;
  Renderer* m_renderer;
  GameVisual m_visual;

  bool m_open;
  bool m_mouseWasDown;
  float m_animElapsed;
  std::string m_title;
  std::string m_message;
  float m_fontSize;
  float m_panelWidth;
  float m_panelHeight;

  std::vector<GuiButtonDef> m_buttons;
  std::vector<float> m_buttonX;
  float m_buttonY;
  float m_buttonWidth;
  float m_buttonHeight;

  int m_selectedButton;
  int m_hoveredButton;
  float m_selectionFromButton;
  float m_selectionAnimElapsed;

  float m_panelX;
  float m_panelY;
  float m_mouseX;
  float m_mouseY;

  int activateSelected() const;
};
