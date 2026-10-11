#pragma once

// Tells the product's software pointer what it is over. Reusable GUI code
// calls markInteractive() on each frame the pointer rests on a button, row or
// tab, and a canvas calls markBrush() while the pointer can paint (with the
// colors its left and right buttons paint); the
// pointer's owner reads and clears the mark once per frame with take(), after
// the scenes have updated. Something pressable wins over a brush. A mark
// nobody takes simply expires with the next take. Main-thread only.
struct GuiPointerHintState
{
  enum class Kind
  {
    Arrow,
    Interactive,
    Brush
  };
  Kind kind = Kind::Arrow;
  // The brush's color, for Kind::Brush.
  unsigned char red = 255;
  unsigned char green = 255;
  unsigned char blue = 255;
  // The color its eraser (right button) paints; the brush's unless given.
  unsigned char eraseRed = 255;
  unsigned char eraseGreen = 255;
  unsigned char eraseBlue = 255;
};

class GuiPointerHint
{
public:
  static void markInteractive()
  {
    s_state.kind = GuiPointerHintState::Kind::Interactive;
  }
  static void markBrush(unsigned char red,
                        unsigned char green,
                        unsigned char blue)
  {
    markBrush(red, green, blue, red, green, blue);
  }
  static void markBrush(unsigned char red,
                        unsigned char green,
                        unsigned char blue,
                        unsigned char eraseRed,
                        unsigned char eraseGreen,
                        unsigned char eraseBlue)
  {
    if (s_state.kind == GuiPointerHintState::Kind::Interactive) {
      return;
    }
    s_state.kind = GuiPointerHintState::Kind::Brush;
    s_state.red = red;
    s_state.green = green;
    s_state.blue = blue;
    s_state.eraseRed = eraseRed;
    s_state.eraseGreen = eraseGreen;
    s_state.eraseBlue = eraseBlue;
  }
  // What was marked since the last take(); clears the mark.
  static GuiPointerHintState take()
  {
    const GuiPointerHintState marked = s_state;
    s_state = GuiPointerHintState{};
    return marked;
  }

private:
  static inline GuiPointerHintState s_state{};
};
