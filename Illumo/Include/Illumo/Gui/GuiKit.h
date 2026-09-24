#pragma once

#include <Illumo/Gui/GuiTextEdit.h>
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
  // X origin at which a '|' caret sits flush after the given text.
  static float caretOriginAfterText(const std::string& text,
                                    float textX,
                                    float sizePt);
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
  // Text whose weight follows `emphasis` on the product's UI weight ramp
  // (FontWeightRamp::ui()): rest weight at 0, emphasis weight at 1 (hovered
  // or focused), bolder still through a spring's overshoot. Without a ramp it
  // is plain default-font text. Returns the text index.
  static size_t drawEmphasizedText(GameVisual& visual,
                                   const std::string& text,
                                   float x,
                                   float y,
                                   float sizePt,
                                   ColorRgba color,
                                   float emphasis);
  static void drawEmphasizedTextCentered(GameVisual& visual,
                                         const std::string& text,
                                         float centerX,
                                         float centerY,
                                         float sizePt,
                                         ColorRgba color,
                                         float emphasis);
  static float measureEmphasizedText(const std::string& text,
                                     float sizePt,
                                     float emphasis);
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
  static void drawRoundedRect(GameVisual& visual,
                              float x,
                              float y,
                              float width,
                              float height,
                              float radius,
                              ColorRgba color);
  static void drawRoundedPanel(GameVisual& visual,
                               float x,
                               float y,
                               float width,
                               float height,
                               unsigned char opacity = 255);

  // A stroked path through `points` whose segments meet in mitered joins, so
  // corners are solid instead of notched like separately drawn lines. Open
  // paths may extend their ends by half the thickness (square caps) so a
  // stroke that ends on another reaches across it. Joins sharper than about
  // 30 degrees cap their miter. One quad per segment; at most 32 points.
  static void drawPolyline(GameVisual& visual,
                           const GuiPoint2* points,
                           int pointCount,
                           float thickness,
                           ColorRgba color,
                           bool closed = false,
                           bool squareCaps = false);
  // A V-shaped chevron whose two arms meet in one mitered corner (no notch
  // at the tip): the tip sits at (centerX, tipY) and the arm ends at
  // centerX -/+ halfWidth, tipY + depth. A positive depth points it up, a
  // negative one down. Two quads.
  static void drawChevron(GameVisual& visual,
                          float centerX,
                          float tipY,
                          float halfWidth,
                          float depth,
                          float thickness,
                          ColorRgba color);

  // --- Living-glass chrome (per-vertex-color shapes) ---
  // Rounded rects and bands subdivide each corner by its radius: 15-degree
  // steps through radius 15 (the quad counts below), finer beyond it.
  // Rounded rect whose color blends from `top` to `bottom` (13 quads).
  static void drawRoundedGradientRect(GameVisual& visual,
                                      float x,
                                      float y,
                                      float width,
                                      float height,
                                      float radius,
                                      ColorRgba top,
                                      ColorRgba bottom);
  // Ring between the rounded rect grown by `innerOffset` and by `outerOffset`
  // (negative offsets inset), blending innerColor to outerColor (28 quads).
  // The shared core of outlines, soft shadows and glows.
  static void drawRoundedBand(GameVisual& visual,
                              float x,
                              float y,
                              float width,
                              float height,
                              float radius,
                              float innerOffset,
                              float outerOffset,
                              ColorRgba innerColor,
                              ColorRgba outerColor);
  static void drawRoundedOutline(GameVisual& visual,
                                 float x,
                                 float y,
                                 float width,
                                 float height,
                                 float radius,
                                 float thickness,
                                 ColorRgba color);
  // Soft shadow falling away from the rounded rect's edge over `spread`.
  static void drawSoftShadow(GameVisual& visual,
                             float x,
                             float y,
                             float width,
                             float height,
                             float radius,
                             float spread,
                             float offsetY,
                             ColorRgba color);
  // Radial glow: `color` at the center fading to transparent at the rim.
  static void drawSoftGlow(GameVisual& visual,
                           float centerX,
                           float centerY,
                           float radiusX,
                           float radiusY,
                           ColorRgba color,
                           int segments = 20);
  // Full-screen radial scrim from `center` to `edge` at the corners.
  static void drawVignette(GameVisual& visual,
                           float width,
                           float height,
                           ColorRgba center,
                           ColorRgba edge,
                           float innerFraction = 0.4f);
  // Diagonal highlight band sweeping left to right as progress goes 0..1,
  // kept inside [x + inset, x + width - inset].
  static void drawSheen(GameVisual& visual,
                        float x,
                        float y,
                        float width,
                        float height,
                        float inset,
                        float progress,
                        ColorRgba color);
  static void drawGlassPanel(GameVisual& visual,
                             float x,
                             float y,
                             float width,
                             float height,
                             const GuiGlassStyle& style);
  // Liquid selection drop (see GuiLiquidSelection): glow, rim, gradient face
  // and arrival sheen. Slices never overlap, so translucent faces stay even.
  static void drawLiquidSelection(GameVisual& visual,
                                  const GuiLiquidSelection& selection);
  // Liquid splash for a press: a ring swells out of the point while a few
  // droplets leap, arc under gravity and fade. progress 0..1; outside that
  // range nothing is drawn.
  static void drawSplash(GameVisual& visual,
                         float centerX,
                         float centerY,
                         float progress,
                         float scale,
                         ColorRgba color);
  // Raised keyboard key chip. Labels UP, DOWN, LEFT, RIGHT, UPDOWN and
  // LEFTRIGHT draw arrow glyphs. Returns the chip width.
  static float drawKeycap(GameVisual& visual,
                          float x,
                          float y,
                          const std::string& label,
                          float sizePt,
                          unsigned char opacity = 255);
  // Keycap followed by a muted action label; returns the advance to the next
  // hint (including trailing spacing).
  static float drawKeyHint(GameVisual& visual,
                           float x,
                           float y,
                           const std::string& key,
                           const std::string& action,
                           float sizePt,
                           unsigned char opacity = 255);
  static float measureKeyHint(const std::string& key,
                              const std::string& action,
                              float sizePt);

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
  // A single-line field. With an active edit it draws the edit's text,
  // selection and blinking caret (scrolled to keep the caret visible);
  // otherwise the value, truncated to fit. Invalid edits get a red border;
  // read-only fields are muted and borderless.
  static void drawTextField(GameVisual& visual,
                            float x,
                            float y,
                            float w,
                            float h,
                            const std::string& value,
                            float fontSize,
                            const GuiTextEdit* edit,
                            bool invalid,
                            bool hovered,
                            bool readOnly);

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
