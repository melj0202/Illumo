// Screen-space string composed as TextPrimitive on an embedded GameVisual.
#pragma once

#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/Primitives/UiTheme.h>
#include <cstdint>
#include <string>

class IRenderWindow;
class Renderer;

class GLString : public Drawable<GLString>
{
private:
  std::string content;
  int r, g, b, a;
  float size_pt;
  int x, y;
  bool contentDirty;
  // One-pixel black copies of the glyphs, so the text stays readable on a
  // bright scene without a filled panel.
  bool glyphBacking;

  Renderer* renderer;
  GameVisual visual;
  UiPanelStyle panelStyle;

  void syncVisual();
  void markContentDirty() { contentDirty = true; }

public:
  static inline IRenderWindow* s_window = nullptr;
  static void setRenderWindow(IRenderWindow* window) { s_window = window; }

  GLString();
  GLString(std::string content,
           int r,
           int g,
           int b,
           int a,
           int size_pt,
           int x,
           int y,
           Renderer* renderer = nullptr);
  ~GLString();

  void setRenderer(Renderer* r);
  void setContent(std::string newContent);
  void setR(int newR);
  void setG(int newG);
  void setB(int newB);
  void setA(int newA);
  void setSize(int newSize);
  void setX(int newX);
  void setY(int newY);
  void setPanelStyle(const UiPanelStyle& style);
  void clearPanelStyle();
  void setGlyphBacking(bool enabled);
  void DrawImpl();
  bool AppendCommands(Renderer* renderer) override;

  GameVisual& getVisual() { return visual; }
  const GameVisual& getVisual() const { return visual; }

  std::string getContent();
  int getR();
  int getG();
  int getB();
  int getA();
  int getSize();
  int getX();
  int getY();
};
