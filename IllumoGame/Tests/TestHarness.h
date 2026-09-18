#pragma once
#include "Game/Canvas.h"
#include <Illumo/Testing/TestHarness.h>

// Owns mock renderer stack so Canvas/rules can run without GL.
struct HeadlessCanvasFixture
{
  NullRenderWindow window;
  EnvVars env;
  Camera camera;
  MockBackend mock;
  Renderer renderer;
  Canvas* canvas;

  HeadlessCanvasFixture(int canvasW,
                        int canvasH,
                        int winW = 1280,
                        int winH = 720)
    : window(winW, winH)
    , env()
    , camera(glm::vec2(0.0f, 0.0f), 1.0f, &env)
    , mock()
    , renderer(&window, &env, &camera, &mock, false)
    , canvas(nullptr)
  {
    env.setVar("WinX", winW);
    env.setVar("WinY", winH);
    env.setVar("CanvasX", canvasW);
    env.setVar("CanvasY", canvasH);
    mock.Initialize();
    canvas = new Canvas(canvasW, canvasH, &window, &camera, &renderer);
  }

  ~HeadlessCanvasFixture()
  {
    delete canvas;
    canvas = nullptr;
  }

  // Game of Life convention in this codebase: 0 = alive, 1 = dead.
  static constexpr unsigned char Alive = 0;
  static constexpr unsigned char Dead = 1;

  void clearDead() { canvas->clearCanvas(); }

  void setAlive(int x, int y) { canvas->setCanvasPixel(x, y, Alive); }

  void setDead(int x, int y) { canvas->setCanvasPixel(x, y, Dead); }

  unsigned char at(int x, int y) { return canvas->getCanvasPixel(x, y); }

  bool isAlive(int x, int y) { return at(x, y) == Alive; }
};
