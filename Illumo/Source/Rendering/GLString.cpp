#include "thirdparty/stb/stb_easy_font.h"
#include <Illumo/Rendering/GLString.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/Logger.h>

GLString::GLString()
  : content("")
  , r(255)
  , g(255)
  , b(255)
  , a(255)
  , size_pt(12.0f)
  , x(0)
  , y(0)
  , contentDirty(true)
  , renderer(nullptr)
{
  visual.setSpace(PrimitiveSpace::Pixels);
  visual.setLayerHint(RenderLayerId::UI);
}

GLString::GLString(std::string content,
                   int r,
                   int g,
                   int b,
                   int a,
                   int size_pt,
                   int x,
                   int y,
                   Renderer* renderer)
  : content(content)
  , r(r)
  , g(g)
  , b(b)
  , a(a)
  , size_pt(static_cast<float>(size_pt))
  , x(x)
  , y(y)
  , contentDirty(true)
  , renderer(renderer)
{
  visual.setSpace(PrimitiveSpace::Pixels);
  visual.setLayerHint(RenderLayerId::UI);
  if (renderer) {
    visual.setRenderer(renderer);
    visual.prepare(renderer);
  }
  if (s_window) {
    visual.setWindow(s_window);
  }
  syncVisual();
}

GLString::~GLString() = default;

void
GLString::setRenderer(Renderer* rend)
{
  renderer = rend;
  visual.setRenderer(rend);
  if (renderer) {
    visual.prepare(renderer);
  }
  markContentDirty();
}

void
GLString::syncVisual()
{
  visual.clearPrimitives();
  if (!content.empty()) {
    ColorRgba color{ static_cast<unsigned char>(r),
                     static_cast<unsigned char>(g),
                     static_cast<unsigned char>(b),
                     static_cast<unsigned char>(a) };
    float textX = static_cast<float>(x);
    float textY = static_cast<float>(y);
    if (panelStyle.enabled) {
      std::string measuredContent = content;
      for (char& ch : measuredContent) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (uch != '\n' && (uch < 32 || uch > 126)) {
          ch = '?';
        }
      }
      const float scale = size_pt / 12.0f;
      const float textWidth =
        static_cast<float>(stb_easy_font_width(measuredContent.data())) * scale;
      const float textHeight =
        static_cast<float>(stb_easy_font_height(measuredContent.data())) *
        scale;
      const float accentGap = panelStyle.accentWidth + 7.0f;
      const float panelX = textX;
      const float panelY = textY;
      const float panelW = panelStyle.paddingX * 2.0f + accentGap + textWidth;
      const float panelH = panelStyle.paddingY * 2.0f + textHeight;
      const unsigned char opacity = static_cast<unsigned char>(a);

      visual.addFilledRect(panelX + panelStyle.shadowOffset,
                           panelY + panelStyle.shadowOffset,
                           panelW,
                           panelH,
                           UiTheme::applyOpacity(panelStyle.shadow, opacity));
      visual.addFilledRect(
        panelX,
        panelY,
        panelW,
        panelH,
        UiTheme::applyOpacity(panelStyle.background, opacity));
      visual.addOutlineRect(panelX,
                            panelY,
                            panelW,
                            panelH,
                            UiTheme::applyOpacity(panelStyle.border, opacity),
                            panelStyle.borderWidth);
      visual.addFilledRect(panelX,
                           panelY,
                           panelStyle.accentWidth,
                           panelH,
                           UiTheme::applyOpacity(panelStyle.accent, opacity));
      textX += panelStyle.paddingX + accentGap;
      textY += panelStyle.paddingY;
    }
    visual.addText(content, textX, textY, size_pt, color);
  }
  contentDirty = false;
}

void
GLString::setContent(std::string newContent)
{
  if (content != newContent) {
    content = newContent;
    markContentDirty();
  }
}

void
GLString::setR(int newR)
{
  if (r != newR) {
    r = newR;
    markContentDirty();
  }
}
void
GLString::setG(int newG)
{
  if (g != newG) {
    g = newG;
    markContentDirty();
  }
}
void
GLString::setB(int newB)
{
  if (b != newB) {
    b = newB;
    markContentDirty();
  }
}
void
GLString::setA(int newA)
{
  if (a != newA) {
    a = newA;
    markContentDirty();
  }
}
void
GLString::setSize(int newSize)
{
  float s = static_cast<float>(newSize);
  if (size_pt != s) {
    size_pt = s;
    markContentDirty();
  }
}
void
GLString::setX(int newX)
{
  if (x != newX) {
    x = newX;
    markContentDirty();
  }
}
void
GLString::setY(int newY)
{
  if (y != newY) {
    y = newY;
    markContentDirty();
  }
}

void
GLString::setPanelStyle(const UiPanelStyle& style)
{
  panelStyle = style;
  markContentDirty();
}

void
GLString::clearPanelStyle()
{
  panelStyle = UiPanelStyle{};
  markContentDirty();
}

std::string
GLString::getContent()
{
  return content;
}
int
GLString::getR()
{
  return r;
}
int
GLString::getG()
{
  return g;
}
int
GLString::getB()
{
  return b;
}
int
GLString::getA()
{
  return a;
}
int
GLString::getSize()
{
  return static_cast<int>(size_pt);
}
int
GLString::getX()
{
  return x;
}
int
GLString::getY()
{
  return y;
}

void
GLString::DrawImpl()
{
}

bool
GLString::AppendCommands(Renderer* rend)
{
  if (!isVisible()) {
    return true;
  }
  if (content.empty()) {
    return true;
  }
  if (!rend) {
    return false;
  }
  if (!s_window) {
    return true;
  }

  if (contentDirty || renderer != rend) {
    renderer = rend;
    syncVisual();
  }

  visual.setRenderer(rend);
  visual.setWindow(s_window);
  visual.setSpace(PrimitiveSpace::Pixels);
  visual.setVisible(true);
  return visual.AppendCommands(rend);
}
