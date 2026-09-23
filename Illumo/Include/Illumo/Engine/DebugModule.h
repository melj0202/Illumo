#pragma once
#include <Illumo/Engine/IModule.h>
#include <Illumo/Rendering/GLString.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/Primitives/SpriteAnimation.h>
#include <Illumo/Services/KeyCode.h>

#include <memory>

class DebugOverlayState;
class FrameProfiler;
class PixelWindow;
class ProfilerOverlay;
class SoftwareCanvas;

class DebugModule : public IModule
{
public:
  explicit DebugModule(FrameProfiler* profiler = nullptr);
  ~DebugModule();
  DebugModule(const DebugModule&) = delete;
  DebugModule& operator=(const DebugModule&) = delete;
  DebugModule(DebugModule&&) = delete;
  DebugModule& operator=(DebugModule&&) = delete;
  virtual bool Start(IllumoContext* context) override;
  void Update(double dt) override;
  void DispatchDrawables(Scene* scene) override;
  void Exit() override;

private:
  FrameProfiler* m_profiler;
  std::unique_ptr<ProfilerOverlay> m_profilerOverlay;
  void updateDiagnostics(double dt);
  void updateWatermarkPosition();
  void registerRendererCommands();
  void unregisterRendererCommands();
  void createRendererDemo();

  // Console editing keys shared by the game window and the detached window.
  void routeConsoleKey(KeyCode key, InputAction action, int modifiers);
  // Detached console window (D-UI5): created on request, pumped, drawn by a
  // software canvas, and destroyed on dock/close/exit.
  void processConsoleWindowRequest();
  void detachConsole(int originX, int originY, int width, int height);
  void closeDetachedConsole(bool reopenInGame);
  void updateDetachedConsole();

  std::unique_ptr<PixelWindow> m_consoleWindow;
  std::unique_ptr<SoftwareCanvas> m_consoleCanvas;
  bool m_consoleMouseDown = false;
  bool m_detachedMouseDown = false;

  GLString* diagnosticsLabel;
  std::unique_ptr<DebugOverlayState> diagnostics;
  GLString* watermarkLabel;
  GameVisual* rendererDemo;
  TextureHandle rendererDemoTexture{};
  ShaderHandle rendererDemoShader{};
  RenderStyleHandle rendererDemoStyle{};
  SpriteAnimationClip rendererDemoClip;
  SpriteAnimator rendererDemoAnimator;
  size_t animatedSpriteIndex;
  size_t rotatingSpriteIndex;
  bool rendererDemoEnabled;
  double rendererDemoRotation;
};
