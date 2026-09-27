#pragma once
#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Rendering/GLString.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/Primitives/SpriteAnimation.h>
#include <Illumo/Services/KeyCode.h>

#include <memory>

class DebugOverlayState;
class FileTreeOverlay;
class FrameProfiler;
class PixelWindow;
class ProfilerOverlay;
class SoftwareCanvas;

// The developer overlay of debug-tool builds (ILLUMO_ENABLE_DEBUG_TOOLS): the
// console and its detached window, the diagnostics and profiler overlays, the
// `files` browser, the watermark and the renderer demo. The runtime updates it
// before the program, so console keys come first, and dispatches it after the
// program, so it draws on top.
class DebugOverlay
{
public:
  explicit DebugOverlay(FrameProfiler* profiler = nullptr);
  ~DebugOverlay();
  DebugOverlay(const DebugOverlay&) = delete;
  DebugOverlay& operator=(const DebugOverlay&) = delete;
  DebugOverlay(DebugOverlay&&) = delete;
  DebugOverlay& operator=(DebugOverlay&&) = delete;
  // False when the context lacks the services it needs (D-E5).
  bool start(IllumoContext& context);
  void update(double dt);
  void dispatch(DrawList& scene);
  void stop();

private:
  IllumoContext* ic{ nullptr };
  FrameProfiler* m_profiler;
  std::unique_ptr<ProfilerOverlay> m_profilerOverlay;
  // The `files` browser over IllumoContext::fileTree.
  std::unique_ptr<FileTreeOverlay> m_fileTreeOverlay;
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
