#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Gui/GuiTextEdit.h>
#include <Illumo/Gui/GuiToolStyle.h>
#include <Illumo/Rendering/Font.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <algorithm>
#include <cmath>
#include <memory>

float
GuiToolStyle::textWidth(const std::string& value, float size)
{
  if (value.empty()) {
    return 0.0f;
  }
  std::shared_ptr<Font> font = Font::getDefaultFont();
  if (font == nullptr) {
    return GuiKit::estimateTextWidth(value, size);
  }
  return font->measureText(value, size).width;
}

void
GuiToolStyle::text(GameVisual& visual,
                   const std::string& value,
                   float x,
                   float y,
                   float size,
                   ColorRgba color)
{
  if (!value.empty()) {
    visual.addText(value, std::round(x), std::round(y), size, color);
  }
}

// Drops trailing characters (never half a UTF-8 sequence) until the text
// plus an ellipsis fits.
static std::string
fitted(const std::string& value, float width, float size)
{
  if (GuiToolStyle::textWidth(value, size) <= width) {
    return value;
  }
  std::string shown = value;
  while (!shown.empty() &&
         GuiToolStyle::textWidth(shown + "...", size) > width) {
    shown.pop_back();
    while (!shown.empty() &&
           (static_cast<unsigned char>(shown.back()) & 0xc0u) == 0x80u) {
      shown.pop_back();
    }
    if (!shown.empty() && static_cast<unsigned char>(shown.back()) >= 0xc0u) {
      shown.pop_back();
    }
  }
  return shown.empty() ? std::string() : shown + "...";
}

void
GuiToolStyle::fittedText(GameVisual& visual,
                         const std::string& value,
                         float x,
                         float y,
                         float width,
                         float size,
                         ColorRgba color)
{
  if (width > 0.0f) {
    text(visual, fitted(value, width, size), x, y, size, color);
  }
}

static float
centredTextY(float y, float height, float size)
{
  return y + std::max(0.0f, std::round((height - size) * 0.5f)) - 1.0f;
}

void
GuiToolStyle::panel(GameVisual& visual, const GuiToolRect& rect)
{
  visual.addFilledRect(rect.x, rect.y, rect.w, rect.h, GuiToolPalette::panel);
  visual.addOutlineRect(
    rect.x, rect.y, rect.w, rect.h, GuiToolPalette::border, 1.0f);
}

GuiToolRect
GuiToolStyle::titleButtonRect(const GuiToolRect& bar, int index)
{
  const float size = bar.h - 4.0f;
  return { bar.x + bar.w - 2.0f - (size + 2.0f) * static_cast<float>(index + 1),
           bar.y + 2.0f,
           size,
           size };
}

// Small glyphs for the title bar buttons, drawn with lines.
static void
titleGlyph(GameVisual& visual, const GuiToolRect& box, GuiToolTitleButton kind)
{
  const float left = box.x + 4.0f;
  const float top = box.y + 4.0f;
  const float right = box.x + box.w - 4.0f;
  const float bottom = box.y + box.h - 4.0f;
  const ColorRgba color = GuiToolPalette::text;
  if (kind == GuiToolTitleButton::Hide) {
    visual.addLine(left, top, right, bottom, color, 1.0f);
    visual.addLine(right, top, left, bottom, color, 1.0f);
  } else if (kind == GuiToolTitleButton::PopOut) {
    // A window with an arrow leaving its top-right corner.
    const float middleX = (left + right) * 0.5f;
    const float middleY = (top + bottom) * 0.5f;
    visual.addOutlineRect(
      left, middleY, middleX - left + 1.0f, bottom - middleY, color, 1.0f);
    visual.addLine(middleX, middleY, right, top, color, 1.0f);
    visual.addLine(right - 3.0f, top, right, top, color, 1.0f);
    visual.addLine(right, top, right, top + 3.0f, color, 1.0f);
  } else if (kind == GuiToolTitleButton::Dock) {
    // A window with an arrow entering it.
    visual.addOutlineRect(left, top, right - left, bottom - top, color, 1.0f);
    visual.addFilledRect(left, top, right - left, 2.0f, color);
  }
}

void
GuiToolStyle::titleBar(GameVisual& visual,
                       const GuiToolRect& rect,
                       const std::string& title,
                       const std::vector<GuiToolTitleButton>& buttons,
                       int hoveredButton)
{
  visual.addFilledRect(rect.x, rect.y, rect.w, rect.h, GuiToolPalette::bar);
  visual.addLine(rect.x,
                 rect.y + rect.h - 0.5f,
                 rect.x + rect.w,
                 rect.y + rect.h - 0.5f,
                 GuiToolPalette::rule,
                 1.0f);
  const float buttonsWidth =
    static_cast<float>(buttons.size()) * (rect.h - 2.0f) + 4.0f;
  fittedText(visual,
             title,
             rect.x + kPad,
             centredTextY(rect.y, rect.h, kSmallFontSize),
             rect.w - kPad * 2.0f - buttonsWidth,
             kSmallFontSize,
             GuiToolPalette::dim);
  for (std::size_t index = 0; index < buttons.size(); ++index) {
    if (buttons[index] == GuiToolTitleButton::None) {
      continue;
    }
    const GuiToolRect box = titleButtonRect(rect, static_cast<int>(index));
    if (static_cast<int>(index) == hoveredButton) {
      visual.addFilledRect(
        box.x, box.y, box.w, box.h, GuiToolPalette::barHover);
    }
    titleGlyph(visual, box, buttons[index]);
  }
}

void
GuiToolStyle::splitter(GameVisual& visual,
                       const GuiToolRect& rect,
                       bool hovered,
                       bool active)
{
  visual.addFilledRect(rect.x,
                       rect.y,
                       rect.w,
                       rect.h,
                       active    ? GuiToolPalette::accent
                       : hovered ? GuiToolPalette::border
                                 : GuiToolPalette::window);
}

void
GuiToolStyle::sectionHeader(GameVisual& visual,
                            const GuiToolRect& rect,
                            const std::string& label)
{
  fittedText(visual,
             label,
             rect.x + kPad,
             centredTextY(rect.y, rect.h, kSmallFontSize),
             rect.w - kPad * 2.0f,
             kSmallFontSize,
             GuiToolPalette::faint);
  visual.addLine(rect.x + kPad,
                 rect.y + rect.h - 0.5f,
                 rect.x + rect.w - kPad,
                 rect.y + rect.h - 0.5f,
                 GuiToolPalette::rule,
                 1.0f);
}

void
GuiToolStyle::row(GameVisual& visual,
                  const GuiToolRect& rect,
                  bool selected,
                  bool hovered)
{
  if (selected) {
    visual.addFilledRect(
      rect.x, rect.y, rect.w, rect.h, GuiToolPalette::selection);
    visual.addFilledRect(rect.x, rect.y, 2.0f, rect.h, GuiToolPalette::accent);
  } else if (hovered) {
    visual.addFilledRect(rect.x, rect.y, rect.w, rect.h, GuiToolPalette::hover);
  }
}

void
GuiToolStyle::button(GameVisual& visual,
                     const GuiToolRect& rect,
                     const std::string& label,
                     bool hovered,
                     bool active)
{
  visual.addFilledRect(rect.x,
                       rect.y,
                       rect.w,
                       rect.h,
                       active    ? GuiToolPalette::selection
                       : hovered ? GuiToolPalette::pressed
                                 : GuiToolPalette::button);
  visual.addOutlineRect(rect.x,
                        rect.y,
                        rect.w,
                        rect.h,
                        active ? GuiToolPalette::accent
                               : GuiToolPalette::border,
                        1.0f);
  const std::string shown = fitted(label, rect.w - 8.0f, kFontSize);
  const float width = textWidth(shown, kFontSize);
  text(visual,
       shown,
       rect.x + std::max(4.0f, (rect.w - width) * 0.5f),
       centredTextY(rect.y, rect.h, kFontSize),
       kFontSize,
       GuiToolPalette::text);
}

void
GuiToolStyle::toggle(GameVisual& visual,
                     const GuiToolRect& rect,
                     const std::string& label,
                     bool on,
                     bool hovered)
{
  if (hovered) {
    visual.addFilledRect(rect.x, rect.y, rect.w, rect.h, GuiToolPalette::hover);
  }
  const float box = std::min(12.0f, rect.h - 6.0f);
  const float boxX = rect.x + kPad;
  const float boxY = rect.y + std::round((rect.h - box) * 0.5f);
  visual.addFilledRect(boxX, boxY, box, box, GuiToolPalette::field);
  visual.addOutlineRect(boxX,
                        boxY,
                        box,
                        box,
                        on ? GuiToolPalette::accent : GuiToolPalette::border,
                        1.0f);
  if (on) {
    visual.addFilledRect(
      boxX + 3.0f, boxY + 3.0f, box - 6.0f, box - 6.0f, GuiToolPalette::accent);
  }
  fittedText(visual,
             label,
             boxX + box + 6.0f,
             centredTextY(rect.y, rect.h, kFontSize),
             rect.w - box - kPad * 2.0f - 6.0f,
             kFontSize,
             GuiToolPalette::text);
}

GuiToolRect
GuiToolStyle::sliderTrack(const GuiToolRect& rect)
{
  const float labelWidth = std::min(110.0f, rect.w * 0.4f);
  // Room for values such as "0.0010" at the small font.
  const float valueWidth = 60.0f;
  return { rect.x + kPad + labelWidth,
           rect.y + rect.h * 0.5f - 2.0f,
           std::max(8.0f, rect.w - labelWidth - valueWidth - kPad * 2.0f),
           4.0f };
}

void
GuiToolStyle::slider(GameVisual& visual,
                     const GuiToolRect& rect,
                     const std::string& label,
                     float fraction,
                     const std::string& value,
                     bool hovered,
                     bool active)
{
  if (hovered || active) {
    visual.addFilledRect(rect.x, rect.y, rect.w, rect.h, GuiToolPalette::hover);
  }
  const GuiToolRect track = sliderTrack(rect);
  fittedText(visual,
             label,
             rect.x + kPad,
             centredTextY(rect.y, rect.h, kFontSize),
             track.x - rect.x - kPad - 4.0f,
             kFontSize,
             GuiToolPalette::text);
  const float clamped = std::clamp(fraction, 0.0f, 1.0f);
  visual.addFilledRect(
    track.x, track.y, track.w, track.h, GuiToolPalette::scrollTrack);
  visual.addFilledRect(
    track.x, track.y, track.w * clamped, track.h, GuiToolPalette::accent);
  const float knob = 8.0f;
  visual.addFilledRect(track.x + track.w * clamped - knob * 0.5f,
                       track.y - (knob - track.h) * 0.5f,
                       knob,
                       knob,
                       active ? GuiToolPalette::caret : GuiToolPalette::text);
  fittedText(visual,
             value,
             track.x + track.w + 8.0f,
             centredTextY(rect.y, rect.h, kSmallFontSize),
             rect.x + rect.w - track.x - track.w - 8.0f - kPad,
             kSmallFontSize,
             GuiToolPalette::dim);
}

void
GuiToolStyle::labelValue(GameVisual& visual,
                         const GuiToolRect& rect,
                         const std::string& label,
                         const std::string& value)
{
  const float labelWidth = std::min(110.0f, rect.w * 0.45f);
  fittedText(visual,
             label,
             rect.x + kPad,
             centredTextY(rect.y, rect.h, kFontSize),
             labelWidth - 4.0f,
             kFontSize,
             GuiToolPalette::dim);
  fittedText(visual,
             value,
             rect.x + kPad + labelWidth,
             centredTextY(rect.y, rect.h, kFontSize),
             rect.w - labelWidth - kPad * 2.0f,
             kFontSize,
             GuiToolPalette::text);
}

void
GuiToolStyle::textField(GameVisual& visual,
                        const GuiToolRect& rect,
                        const std::string& value,
                        const GuiTextEdit* edit,
                        bool invalid,
                        bool hovered,
                        bool readOnly,
                        float fontSize)
{
  const bool focused = edit != nullptr && edit->active();
  visual.addFilledRect(rect.x,
                       rect.y,
                       rect.w,
                       rect.h,
                       readOnly ? GuiToolPalette::panel
                                : GuiToolPalette::field);
  if (!readOnly) {
    visual.addOutlineRect(rect.x,
                          rect.y,
                          rect.w,
                          rect.h,
                          invalid   ? GuiToolPalette::error
                          : focused ? GuiToolPalette::accent
                          : hovered ? GuiToolPalette::faint
                                    : GuiToolPalette::border,
                          1.0f);
  }
  const float padding = 4.0f;
  const float textY = centredTextY(rect.y, rect.h, fontSize);
  const float room = std::max(1.0f, rect.w - padding * 2.0f);
  const ColorRgba color = readOnly ? GuiToolPalette::dim : GuiToolPalette::text;
  if (!focused) {
    fittedText(visual, value, rect.x + padding, textY, room, fontSize, color);
    return;
  }
  const std::string& editing = edit->text();
  const float caretX = textWidth(editing.substr(0, edit->caret()), fontSize);
  const float offset = std::max(0.0f, caretX - room + 2.0f);
  const float originX = rect.x + padding - offset;
  if (edit->hasSelection()) {
    const float start =
      textWidth(editing.substr(0, edit->selectionStart()), fontSize);
    const float end =
      textWidth(editing.substr(0, edit->selectionEnd()), fontSize);
    const float left = std::max(rect.x + 1.0f, originX + start);
    const float right = std::min(rect.x + rect.w - 1.0f, originX + end);
    if (right > left) {
      visual.addFilledRect(left,
                           rect.y + 2.0f,
                           right - left,
                           rect.h - 4.0f,
                           GuiToolPalette::selection);
    }
  }
  visual.addText(editing, originX, textY, fontSize, color);
  if (edit->caretVisible()) {
    visual.addLine(originX + caretX,
                   rect.y + 3.0f,
                   originX + caretX,
                   rect.y + rect.h - 3.0f,
                   GuiToolPalette::caret,
                   1.0f);
  }
}

void
GuiToolStyle::scrollbar(GameVisual& visual,
                        const GuiToolRect& track,
                        float first,
                        float visible,
                        float total)
{
  if (total <= visible || total <= 0.0f) {
    return;
  }
  visual.addFilledRect(
    track.x, track.y, track.w, track.h, GuiToolPalette::scrollTrack);
  const float length = std::max(16.0f, track.h * visible / total);
  const float travel = track.h - length;
  const float position =
    travel * std::clamp(first / std::max(1.0f, total - visible), 0.0f, 1.0f);
  visual.addFilledRect(
    track.x, track.y + position, track.w, length, GuiToolPalette::scrollThumb);
}

GuiToolRect
GuiToolStyle::menuRect(const GuiToolRect& bar,
                       const std::vector<std::string>& menus,
                       int index)
{
  float x = bar.x + 4.0f;
  for (int position = 0; position < static_cast<int>(menus.size());
       ++position) {
    const float width =
      textWidth(menus[static_cast<std::size_t>(position)], kFontSize) + 16.0f;
    if (position == index) {
      return { x, bar.y, width, bar.h };
    }
    x += width;
  }
  return {};
}

void
GuiToolStyle::menuBar(GameVisual& visual,
                      const GuiToolRect& rect,
                      const std::vector<std::string>& menus,
                      int open,
                      int hovered)
{
  visual.addFilledRect(rect.x, rect.y, rect.w, rect.h, GuiToolPalette::bar);
  visual.addLine(rect.x,
                 rect.y + rect.h - 0.5f,
                 rect.x + rect.w,
                 rect.y + rect.h - 0.5f,
                 GuiToolPalette::border,
                 1.0f);
  for (int index = 0; index < static_cast<int>(menus.size()); ++index) {
    const GuiToolRect item = menuRect(rect, menus, index);
    if (index == open || index == hovered) {
      visual.addFilledRect(item.x,
                           item.y,
                           item.w,
                           item.h,
                           index == open ? GuiToolPalette::selection
                                         : GuiToolPalette::barHover);
    }
    text(visual,
         menus[static_cast<std::size_t>(index)],
         item.x + 8.0f,
         centredTextY(item.y, item.h, kFontSize),
         kFontSize,
         GuiToolPalette::text);
  }
}

float
GuiToolStyle::dropdownWidth(const std::vector<MenuItem>& items)
{
  float labels = 0.0f;
  float hints = 0.0f;
  for (const MenuItem& item : items) {
    labels = std::max(labels, textWidth(item.label, kFontSize));
    hints = std::max(hints, textWidth(item.hint, kSmallFontSize));
  }
  return std::max(160.0f, labels + hints + 56.0f);
}

static float
itemHeight(const GuiToolStyle::MenuItem& item)
{
  return item.separator ? 7.0f : GuiToolStyle::kRowHeight;
}

int
GuiToolStyle::dropdownItemAt(float x,
                             float y,
                             const std::vector<MenuItem>& items,
                             float px,
                             float py)
{
  const float width = dropdownWidth(items);
  if (px < x || px >= x + width) {
    return -1;
  }
  float top = y + 3.0f;
  for (int index = 0; index < static_cast<int>(items.size()); ++index) {
    const MenuItem& item = items[static_cast<std::size_t>(index)];
    const float height = itemHeight(item);
    if (py >= top && py < top + height) {
      return item.separator ? -1 : index;
    }
    top += height;
  }
  return -1;
}

void
GuiToolStyle::dropdown(GameVisual& visual,
                       float x,
                       float y,
                       const std::vector<MenuItem>& items,
                       int hovered)
{
  const float width = dropdownWidth(items);
  float height = 6.0f;
  for (const MenuItem& item : items) {
    height += itemHeight(item);
  }
  visual.addFilledRect(x, y, width, height, GuiToolPalette::bar);
  visual.addOutlineRect(x, y, width, height, GuiToolPalette::border, 1.0f);
  float top = y + 3.0f;
  for (int index = 0; index < static_cast<int>(items.size()); ++index) {
    const MenuItem& item = items[static_cast<std::size_t>(index)];
    const float rowHeight = itemHeight(item);
    if (item.separator) {
      visual.addLine(x + 6.0f,
                     top + 3.5f,
                     x + width - 6.0f,
                     top + 3.5f,
                     GuiToolPalette::rule,
                     1.0f);
      top += rowHeight;
      continue;
    }
    if (index == hovered && item.enabled) {
      visual.addFilledRect(
        x + 2.0f, top, width - 4.0f, rowHeight, GuiToolPalette::selection);
    }
    const ColorRgba color =
      item.enabled ? GuiToolPalette::text : GuiToolPalette::faint;
    if (item.checked) {
      text(visual,
           "*",
           x + 8.0f,
           centredTextY(top, rowHeight, kFontSize),
           kFontSize,
           GuiToolPalette::accent);
    }
    text(visual,
         item.label,
         x + 22.0f,
         centredTextY(top, rowHeight, kFontSize),
         kFontSize,
         color);
    if (!item.hint.empty()) {
      text(visual,
           item.hint,
           x + width - 10.0f - textWidth(item.hint, kSmallFontSize),
           centredTextY(top, rowHeight, kSmallFontSize),
           kSmallFontSize,
           GuiToolPalette::faint);
    }
    top += rowHeight;
  }
}

void
GuiToolStyle::statusBar(GameVisual& visual,
                        const GuiToolRect& rect,
                        const std::string& left,
                        const std::string& right)
{
  visual.addFilledRect(rect.x, rect.y, rect.w, rect.h, GuiToolPalette::bar);
  visual.addLine(rect.x,
                 rect.y + 0.5f,
                 rect.x + rect.w,
                 rect.y + 0.5f,
                 GuiToolPalette::border,
                 1.0f);
  const float rightWidth = textWidth(right, kSmallFontSize);
  fittedText(visual,
             left,
             rect.x + kPad,
             centredTextY(rect.y, rect.h, kSmallFontSize),
             rect.w - rightWidth - kPad * 3.0f,
             kSmallFontSize,
             GuiToolPalette::dim);
  text(visual,
       right,
       rect.x + rect.w - kPad - rightWidth,
       centredTextY(rect.y, rect.h, kSmallFontSize),
       kSmallFontSize,
       GuiToolPalette::dim);
}
