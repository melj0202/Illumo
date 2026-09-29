#pragma once

// Tells the product's software pointer that it is over something the user can
// act on. Reusable GUI code calls markInteractive() on each frame the pointer
// rests on a button, row or tab; the pointer's owner reads and clears the
// mark once per frame with take(), after the scenes have updated. A mark
// nobody takes simply expires with the next take. Main-thread only.
class GuiPointerHint
{
public:
  static void markInteractive() { s_interactive = true; }
  // True when something marked the pointer since the last take().
  static bool take()
  {
    const bool marked = s_interactive;
    s_interactive = false;
    return marked;
  }

private:
  static inline bool s_interactive = false;
};
