#pragma once

#include <Illumo/Gui/GuiTypes.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/Primitives/UiTheme.h>
#include <string>

// Consolidated stateless drawing, layout, and hit-testing helpers for Illumo
// UI. All routines operate directly on GameVisual to preserve painter-correct
// primitive composition without a retained widget tree.
class GuiKit
{
public:
  // --- Text Metrics & Layout ---
  static float estimateTextWidth(const std::string& text, float sizePt);
  static float defaultLineHeight(float sizePt);
  static void drawTextCentered(GameVisual& visual,
                               const std::string& text,
                               float centerX,
                               float centerY,
                               float sizePt,
                               ColorRgba color);
  static void drawTextAligned(GameVisual& visual,
                              const std::string& text,
                              float x,
                              float y,
                              float width,
                              float sizePt,
                              ColorRgba color,
                              GuiAlignment alignment = GuiAlignment::Left);
  static void drawLabelValue(GameVisual& visual,
                             const std::string& label,
                             const std::string& value,
                             float x,
                             float y,
                             float width,
                             float sizePt,
                             ColorRgba labelColor = UiTheme::textMuted(),
                             ColorRgba valueColor = UiTheme::textPrimary());

  // --- Panels, Surfaces & Chrome ---
  static bool isPointInRect(float px,
                            float py,
                            float rx,
                            float ry,
                            float rw,
                            float rh);

  static void drawBackdrop(GameVisual& visual,
                           float screenWidth,
                           float screenHeight,
                           unsigned char opacity = 190);

  static void drawShadow(GameVisual& visual,
                         float x,
                         float y,
                         float w,
                         float h,
                         float offset = 4.0f,
                         ColorRgba color = ColorRgba{ 0, 0, 0, 150 });

  static void drawPanel(GameVisual& visual,
                        float x,
                        float y,
                        float w,
                        float h,
                        const GuiPanelChrome& chrome);

  static void drawCard(GameVisual& visual,
                       float x,
                       float y,
                       float w,
                       float h,
                       ColorRgba surfaceColor = UiTheme::panelSurface(),
                       ColorRgba borderColor = UiTheme::panelBorder(),
                       float borderWidth = 1.0f);

  static void drawHeaderBar(GameVisual& visual,
                            float x,
                            float y,
                            float w,
                            float h,
                            const std::string& title,
                            float fontSize,
                            ColorRgba surfaceColor = UiTheme::panelRaised(),
                            ColorRgba textColor = UiTheme::textPrimary(),
                            ColorRgba accentColor = UiTheme::accent());

  static void drawDivider(GameVisual& visual,
                          float x,
                          float y,
                          float length,
                          bool vertical = false,
                          ColorRgba color = UiTheme::divider());

  // --- Buttons & Highlights ---
  static void drawButton(GameVisual& visual,
                         float x,
                         float y,
                         float w,
                         float h,
                         const std::string& label,
                         float fontSize,
                         GuiButtonState state,
                         ColorRgba customAccent = ColorRgba{});

  static void drawIconButton(GameVisual& visual,
                             float x,
                             float y,
                             float w,
                             float h,
                             TextureHandle atlas,
                             const TextureRegion& iconRegion,
                             const std::string& label,
                             float fontSize,
                             GuiButtonState state,
                             ColorRgba customAccent = ColorRgba{});

  static void drawSelectionHighlight(GameVisual& visual,
                                     float x,
                                     float y,
                                     float w,
                                     float h,
                                     ColorRgba color = UiTheme::selection(),
                                     float outlineWidth = 1.0f);

  // --- Form & Settings Controls ---
  static void drawPropertyRow(GameVisual& visual,
                              float x,
                              float y,
                              float w,
                              float h,
                              const std::string& label,
                              const std::string& value,
                              float fontSize,
                              bool isSelected,
                              bool isHovered);

  static void drawToggle(GameVisual& visual,
                         float x,
                         float y,
                         float w,
                         float h,
                         bool enabled,
                         float fontSize,
                         bool isSelected);

  static void drawSlider(GameVisual& visual,
                         float x,
                         float y,
                         float w,
                         float h,
                         float fraction,
                         const std::string& valueText,
                         float fontSize,
                         bool isSelected);

  static void drawNumericStepper(GameVisual& visual,
                                 float x,
                                 float y,
                                 float w,
                                 float h,
                                 const std::string& valueText,
                                 float fontSize,
                                 bool isSelected);

  // --- Toolbars, Menus & Toasts ---
  static void drawMenuBar(GameVisual& visual,
                          float x,
                          float y,
                          float w,
                          float h,
                          ColorRgba surfaceColor = UiTheme::panelSurface());

  static void drawMenuItem(GameVisual& visual,
                           float x,
                           float y,
                           float w,
                           float h,
                           const std::string& label,
                           const std::string& shortcut,
                           float fontSize,
                           bool isHovered,
                           bool isSelected);

  static void drawStatusBar(GameVisual& visual,
                            float x,
                            float y,
                            float w,
                            float h,
                            const std::string& statusText,
                            float fontSize,
                            ColorRgba textColor = UiTheme::textMuted());

  static void drawToast(GameVisual& visual,
                        float x,
                        float y,
                        float w,
                        float h,
                        const std::string& message,
                        float fontSize,
                        ColorRgba accentColor = UiTheme::accent(),
                        float progress = 1.0f);

  // --- Tree & List Rows ---
  static void drawTreeRow(GameVisual& visual,
                          float x,
                          float y,
                          float w,
                          float h,
                          int depth,
                          const std::string& label,
                          bool isSelected,
                          bool isHovered,
                          bool isDropTarget = false,
                          TextureHandle atlas = TextureHandle{},
                          const TextureRegion& icon = TextureRegion{});
};
