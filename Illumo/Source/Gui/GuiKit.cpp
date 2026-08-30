#include <Illumo/Gui/GuiKit.h>
#include <algorithm>
#include <cmath>

float
GuiKit::estimateTextWidth(const std::string& text, float sizePt)
{
  return static_cast<float>(text.size()) * sizePt * 0.6f;
}

float
GuiKit::defaultLineHeight(float sizePt)
{
  return sizePt * 1.35f;
}

void
GuiKit::drawTextCentered(GameVisual& visual,
                         const std::string& text,
                         float centerX,
                         float centerY,
                         float sizePt,
                         ColorRgba color)
{
  const float textW = estimateTextWidth(text, sizePt);
  const float textX = centerX - textW * 0.5f;
  const float textY = centerY - sizePt * 0.5f;
  visual.addText(text, textX, textY, sizePt, color);
}

void
GuiKit::drawTextAligned(GameVisual& visual,
                        const std::string& text,
                        float x,
                        float y,
                        float width,
                        float sizePt,
                        ColorRgba color,
                        GuiAlignment alignment)
{
  const float textW = estimateTextWidth(text, sizePt);
  float drawX = x;
  if (alignment == GuiAlignment::Center) {
    drawX = x + std::max(0.0f, (width - textW) * 0.5f);
  } else if (alignment == GuiAlignment::Right) {
    drawX = x + std::max(0.0f, width - textW);
  }
  visual.addText(text, drawX, y, sizePt, color);
}

void
GuiKit::drawLabelValue(GameVisual& visual,
                       const std::string& label,
                       const std::string& value,
                       float x,
                       float y,
                       float width,
                       float sizePt,
                       ColorRgba labelColor,
                       ColorRgba valueColor)
{
  visual.addText(label, x, y, sizePt, labelColor);
  const float valW = estimateTextWidth(value, sizePt);
  const float valX = x + std::max(0.0f, width - valW);
  visual.addText(value, valX, y, sizePt, valueColor);
}

bool
GuiKit::isPointInRect(float px,
                      float py,
                      float rx,
                      float ry,
                      float rw,
                      float rh)
{
  return px >= rx && px <= rx + rw && py >= ry && py <= ry + rh;
}

void
GuiKit::drawBackdrop(GameVisual& visual,
                     float screenWidth,
                     float screenHeight,
                     unsigned char opacity)
{
  visual.addFilledRect(
    0.0f, 0.0f, screenWidth, screenHeight, ColorRgba{ 0, 0, 0, opacity });
}

void
GuiKit::drawShadow(GameVisual& visual,
                   float x,
                   float y,
                   float w,
                   float h,
                   float offset,
                   ColorRgba color)
{
  visual.addFilledRect(x + offset, y + offset, w, h, color);
}

void
GuiKit::drawPanel(GameVisual& visual,
                  float x,
                  float y,
                  float w,
                  float h,
                  const GuiPanelChrome& chrome)
{
  if (chrome.drawShadow && chrome.shadowOffset > 0.0f && chrome.shadow.a > 0) {
    drawShadow(visual, x, y, w, h, chrome.shadowOffset, chrome.shadow);
  }
  visual.addFilledRect(x, y, w, h, chrome.background);
  if (chrome.borderWidth > 0.0f && chrome.border.a > 0) {
    visual.addOutlineRect(x, y, w, h, chrome.border, chrome.borderWidth);
  }
  if (chrome.drawAccent && chrome.accentWidth > 0.0f && chrome.accent.a > 0) {
    visual.addFilledRect(x, y, chrome.accentWidth, h, chrome.accent);
  }
}

void
GuiKit::drawCard(GameVisual& visual,
                 float x,
                 float y,
                 float w,
                 float h,
                 ColorRgba surfaceColor,
                 ColorRgba borderColor,
                 float borderWidth)
{
  GuiPanelChrome chrome;
  chrome.background = surfaceColor;
  chrome.border = borderColor;
  chrome.borderWidth = borderWidth;
  chrome.shadow = UiTheme::panelShadow();
  chrome.shadowOffset = 3.0f;
  chrome.drawShadow = true;
  chrome.drawAccent = false;
  drawPanel(visual, x, y, w, h, chrome);
}

void
GuiKit::drawHeaderBar(GameVisual& visual,
                      float x,
                      float y,
                      float w,
                      float h,
                      const std::string& title,
                      float fontSize,
                      ColorRgba surfaceColor,
                      ColorRgba textColor,
                      ColorRgba accentColor)
{
  visual.addFilledRect(x, y, w, h, surfaceColor);
  visual.addOutlineRect(x, y, w, h, UiTheme::panelBorder(), 1.0f);
  if (accentColor.a > 0) {
    visual.addFilledRect(x, y, 3.0f, h, accentColor);
  }
  const float textY = y + (h - fontSize) * 0.5f;
  visual.addText(title, x + 10.0f, textY, fontSize, textColor);
}

void
GuiKit::drawDivider(GameVisual& visual,
                    float x,
                    float y,
                    float length,
                    bool vertical,
                    ColorRgba color)
{
  if (vertical) {
    visual.addLine(x, y, x, y + length, color, 1.0f);
  } else {
    visual.addLine(x, y, x + length, y, color, 1.0f);
  }
}

void
GuiKit::drawButton(GameVisual& visual,
                   float x,
                   float y,
                   float w,
                   float h,
                   const std::string& label,
                   float fontSize,
                   GuiButtonState state,
                   ColorRgba customAccent)
{
  ColorRgba bg = UiTheme::panelSurface();
  ColorRgba border = UiTheme::panelBorder();
  ColorRgba text = UiTheme::textPrimary();
  const ColorRgba activeAccent =
    (customAccent.a > 0) ? customAccent : UiTheme::accent();

  switch (state) {
    case GuiButtonState::Normal:
      bg = UiTheme::panelSurface();
      border = UiTheme::panelBorder();
      text = UiTheme::textPrimary();
      break;
    case GuiButtonState::Hover:
      bg = UiTheme::panelRaised();
      border = activeAccent;
      text = UiTheme::textPrimary();
      break;
    case GuiButtonState::Pressed:
      bg = UiTheme::panelInset();
      border = activeAccent;
      text = activeAccent;
      break;
    case GuiButtonState::Disabled:
      bg = UiTheme::applyOpacity(UiTheme::panelInset(), 140);
      border = UiTheme::applyOpacity(UiTheme::divider(), 120);
      text = UiTheme::textMuted();
      break;
  }

  // Drop shadow for normal / hover states
  if (state != GuiButtonState::Disabled && state != GuiButtonState::Pressed) {
    visual.addFilledRect(x + 2.0f, y + 2.0f, w, h, ColorRgba{ 0, 0, 0, 100 });
  }

  visual.addFilledRect(x, y, w, h, bg);
  visual.addOutlineRect(x, y, w, h, border, 1.0f);

  if (state == GuiButtonState::Hover) {
    visual.addFilledRect(x, y, 3.0f, h, activeAccent);
  }

  drawTextCentered(visual, label, x + w * 0.5f, y + h * 0.5f, fontSize, text);
}

void
GuiKit::drawIconButton(GameVisual& visual,
                       float x,
                       float y,
                       float w,
                       float h,
                       TextureHandle atlas,
                       const TextureRegion& iconRegion,
                       const std::string& label,
                       float fontSize,
                       GuiButtonState state,
                       ColorRgba customAccent)
{
  drawButton(visual, x, y, w, h, "", fontSize, state, customAccent);

  const float iconSize = std::min(w, h) * 0.65f;
  if (label.empty()) {
    const float iconX = x + (w - iconSize) * 0.5f;
    const float iconY = y + (h - iconSize) * 0.5f;
    visual.addSprite(atlas,
                     Rect2{ iconX, iconY, iconSize, iconSize },
                     iconRegion,
                     ColorRgba{ 255, 255, 255, 255 });
  } else {
    const float iconX = x + 6.0f;
    const float iconY = y + (h - iconSize) * 0.5f;
    visual.addSprite(atlas,
                     Rect2{ iconX, iconY, iconSize, iconSize },
                     iconRegion,
                     ColorRgba{ 255, 255, 255, 255 });
    const float textX = iconX + iconSize + 6.0f;
    const float textY = y + (h - fontSize) * 0.5f;
    visual.addText(label, textX, textY, fontSize, UiTheme::textPrimary());
  }
}

void
GuiKit::drawSelectionHighlight(GameVisual& visual,
                               float x,
                               float y,
                               float w,
                               float h,
                               ColorRgba color,
                               float outlineWidth)
{
  visual.addFilledRect(x, y, w, h, color);
  if (outlineWidth > 0.0f) {
    visual.addOutlineRect(x, y, w, h, UiTheme::accent(), outlineWidth);
  }
}

void
GuiKit::drawPropertyRow(GameVisual& visual,
                        float x,
                        float y,
                        float w,
                        float h,
                        const std::string& label,
                        const std::string& value,
                        float fontSize,
                        bool isSelected,
                        bool isHovered)
{
  if (isSelected) {
    drawSelectionHighlight(visual, x, y, w, h, UiTheme::selection(), 1.0f);
  } else if (isHovered) {
    visual.addFilledRect(
      x, y, w, h, UiTheme::applyOpacity(UiTheme::panelRaised(), 160));
  }

  const float textY = y + (h - fontSize) * 0.5f;
  const ColorRgba labelColor =
    isSelected ? UiTheme::textPrimary() : UiTheme::textMuted();
  const ColorRgba valueColor =
    isSelected ? UiTheme::accent() : UiTheme::textPrimary();

  drawLabelValue(visual,
                 label,
                 value,
                 x + 8.0f,
                 textY,
                 w - 16.0f,
                 fontSize,
                 labelColor,
                 valueColor);

  drawDivider(
    visual, x, y + h, w, false, UiTheme::applyOpacity(UiTheme::divider(), 100));
}

void
GuiKit::drawToggle(GameVisual& visual,
                   float x,
                   float y,
                   float w,
                   float h,
                   bool enabled,
                   float fontSize,
                   bool isSelected)
{
  const float trackH = std::min(h * 0.7f, 20.0f);
  const float trackW = trackH * 2.0f;
  const float trackX = x + w - trackW - 8.0f;
  const float trackY = y + (h - trackH) * 0.5f;

  const ColorRgba trackBg = enabled ? UiTheme::accent() : UiTheme::panelInset();
  visual.addFilledRect(trackX, trackY, trackW, trackH, trackBg);
  visual.addOutlineRect(
    trackX, trackY, trackW, trackH, UiTheme::panelBorder(), 1.0f);

  const float knobSize = trackH - 4.0f;
  const float knobX =
    enabled ? (trackX + trackW - knobSize - 2.0f) : (trackX + 2.0f);
  const float knobY = trackY + 2.0f;
  visual.addFilledRect(
    knobX, knobY, knobSize, knobSize, UiTheme::textPrimary());
}

void
GuiKit::drawSlider(GameVisual& visual,
                   float x,
                   float y,
                   float w,
                   float h,
                   float fraction,
                   const std::string& valueText,
                   float fontSize,
                   bool isSelected)
{
  const float clampedFraction = std::clamp(fraction, 0.0f, 1.0f);
  const float trackW = w * 0.45f;
  const float trackH = 6.0f;
  const float trackX = x + w - trackW - 8.0f;
  const float trackY = y + (h - trackH) * 0.5f;

  // Track background
  visual.addFilledRect(trackX, trackY, trackW, trackH, UiTheme::panelInset());
  visual.addOutlineRect(
    trackX, trackY, trackW, trackH, UiTheme::panelBorder(), 1.0f);

  // Active track
  if (clampedFraction > 0.0f) {
    visual.addFilledRect(
      trackX, trackY, trackW * clampedFraction, trackH, UiTheme::accent());
  }

  // Thumb
  const float thumbW = 8.0f;
  const float thumbH = 16.0f;
  const float thumbX = trackX + trackW * clampedFraction - thumbW * 0.5f;
  const float thumbY = y + (h - thumbH) * 0.5f;
  visual.addFilledRect(thumbX, thumbY, thumbW, thumbH, UiTheme::textPrimary());

  // Value text next to track
  if (!valueText.empty()) {
    const float valW = estimateTextWidth(valueText, fontSize);
    const float valX = trackX - valW - 8.0f;
    const float valY = y + (h - fontSize) * 0.5f;
    visual.addText(valueText, valX, valY, fontSize, UiTheme::textPrimary());
  }
}

void
GuiKit::drawNumericStepper(GameVisual& visual,
                           float x,
                           float y,
                           float w,
                           float h,
                           const std::string& valueText,
                           float fontSize,
                           bool isSelected)
{
  const float boxW = 80.0f;
  const float boxH = std::min(h * 0.8f, 24.0f);
  const float boxX = x + w - boxW - 8.0f;
  const float boxY = y + (h - boxH) * 0.5f;

  visual.addFilledRect(boxX, boxY, boxW, boxH, UiTheme::panelInset());
  visual.addOutlineRect(boxX,
                        boxY,
                        boxW,
                        boxH,
                        isSelected ? UiTheme::accent() : UiTheme::panelBorder(),
                        1.0f);

  drawTextCentered(visual,
                   valueText,
                   boxX + boxW * 0.5f,
                   boxY + boxH * 0.5f,
                   fontSize,
                   UiTheme::textPrimary());
}

void
GuiKit::drawMenuBar(GameVisual& visual,
                    float x,
                    float y,
                    float w,
                    float h,
                    ColorRgba surfaceColor)
{
  visual.addFilledRect(x, y, w, h, surfaceColor);
  visual.addLine(x, y + h, x + w, y + h, UiTheme::divider(), 1.0f);
}

void
GuiKit::drawMenuItem(GameVisual& visual,
                     float x,
                     float y,
                     float w,
                     float h,
                     const std::string& label,
                     const std::string& shortcut,
                     float fontSize,
                     bool isHovered,
                     bool isSelected)
{
  if (isHovered || isSelected) {
    visual.addFilledRect(x, y, w, h, UiTheme::selection());
    visual.addFilledRect(x, y, 2.0f, h, UiTheme::accent());
  }

  const float textY = y + (h - fontSize) * 0.5f;
  visual.addText(label, x + 8.0f, textY, fontSize, UiTheme::textPrimary());

  if (!shortcut.empty()) {
    const float scW = estimateTextWidth(shortcut, fontSize * 0.9f);
    const float scX = x + w - scW - 8.0f;
    visual.addText(shortcut, scX, textY, fontSize * 0.9f, UiTheme::textMuted());
  }
}

void
GuiKit::drawStatusBar(GameVisual& visual,
                      float x,
                      float y,
                      float w,
                      float h,
                      const std::string& statusText,
                      float fontSize,
                      ColorRgba textColor)
{
  visual.addFilledRect(x, y, w, h, UiTheme::panelSurface());
  visual.addLine(x, y, x + w, y, UiTheme::divider(), 1.0f);
  const float textY = y + (h - fontSize) * 0.5f;
  visual.addText(statusText, x + 8.0f, textY, fontSize, textColor);
}

void
GuiKit::drawToast(GameVisual& visual,
                  float x,
                  float y,
                  float w,
                  float h,
                  const std::string& message,
                  float fontSize,
                  ColorRgba accentColor,
                  float progress)
{
  const float t = std::clamp(progress, 0.0f, 1.0f);
  const unsigned char alpha = static_cast<unsigned char>(255.0f * t);

  GuiPanelChrome chrome;
  chrome.background = UiTheme::applyOpacity(UiTheme::panelSurface(), alpha);
  chrome.border = UiTheme::applyOpacity(UiTheme::panelBorder(), alpha);
  chrome.accent = UiTheme::applyOpacity(accentColor, alpha);
  chrome.shadow = ColorRgba{ 0, 0, 0, static_cast<unsigned char>(140.0f * t) };
  chrome.shadowOffset = 3.0f;
  chrome.accentWidth = 3.0f;
  chrome.borderWidth = 1.0f;
  chrome.drawShadow = true;
  chrome.drawAccent = true;
  drawPanel(visual, x, y, w, h, chrome);

  const float textY = y + (h - fontSize) * 0.5f;
  visual.addText(message,
                 x + 12.0f,
                 textY,
                 fontSize,
                 UiTheme::applyOpacity(UiTheme::textPrimary(), alpha));
}

void
GuiKit::drawTreeRow(GameVisual& visual,
                    float x,
                    float y,
                    float w,
                    float h,
                    int depth,
                    const std::string& label,
                    bool isSelected,
                    bool isHovered,
                    bool isDropTarget,
                    TextureHandle atlas,
                    const TextureRegion& icon)
{
  if (isSelected) {
    drawSelectionHighlight(visual, x, y, w, h, UiTheme::selection(), 1.0f);
  } else if (isHovered) {
    visual.addFilledRect(
      x, y, w, h, UiTheme::applyOpacity(UiTheme::panelRaised(), 160));
  }

  if (isDropTarget) {
    visual.addOutlineRect(x, y, w, h, UiTheme::accent(), 2.0f);
  }

  const float indent = static_cast<float>(depth) * 16.0f;
  float contentX = x + 8.0f + indent;
  const float centerY = y + h * 0.5f;

  if (icon.u1 > icon.u0 || icon.v1 > icon.v0) {
    const float iconSize = 14.0f;
    const float iconY = centerY - iconSize * 0.5f;
    visual.addSprite(atlas, Rect2{ contentX, iconY, iconSize, iconSize }, icon);
    contentX += iconSize + 6.0f;
  }

  const float textY = y + (h - 13.0f) * 0.5f;
  visual.addText(label,
                 contentX,
                 textY,
                 13.0f,
                 isSelected ? UiTheme::textPrimary() : UiTheme::textMuted());
}
