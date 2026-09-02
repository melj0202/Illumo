#pragma once
#include <Illumo/Foundation/MacroDefs.h>

class Renderer;

// Scene list entry: token emitter (preferred) and/or legacy immediate Draw.
// GL object ownership lives in the backend registries (not here).
// Built-in shader + pipeline defaults live on Renderer styles (D-R14);
// drawables hold content handles and call bindStyle, then emit content tokens.
// Modules place drawables into Scene layers (World / UI / Debug).
// Composed shapes/sprites use GameVisual (D-R15) rather than one Drawable each.
//
// Production pure-token drawables (always AppendCommands → true when visible):
//   Canvas, CommandLine, GLString, SplashText
// Hybrid immediate fallback exists for tests / future stubs only (D-R10).
// CRTP Draw→DrawImpl is leftover for the immediate path; not on the hot token
// path.
class DrawableBase
{
public:
  virtual ~DrawableBase() = default;

  // Immediate-mode path (legacy; used only if AppendCommands returns false).
  virtual void Draw() = 0;

  // Token path (D-R2). Return true if commands were appended and immediate
  // Draw() should be skipped this frame.
  virtual bool AppendCommands(Renderer* renderer)
  {
    (void)renderer;
    return false;
  }

  bool isVisible() const { return visible; }
  void setVisible(bool v) { visible = v; }

  uint32_t getPassMask() const { return passMask; }
  void setPassMask(uint32_t mask) { passMask = mask; }

protected:
  bool visible = true;
  uint32_t passMask = 0xFFFFFFFF;
};

// CRTP helper: virtual Draw -> Derived::DrawImpl (immediate path only).
template<typename Derived>
class Drawable : public DrawableBase
{
public:
  __ILLUMO_FORCE_INLINE__ void Draw() override
  {
    if (isVisible()) {
      static_cast<Derived*>(this)->DrawImpl();
    }
  }
};
