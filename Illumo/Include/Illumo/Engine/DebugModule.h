#pragma once
#include <Illumo/Engine/IModule.h>
#include <Illumo/Rendering/GLString.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/Primitives/SpriteAnimation.h>

class DebugModule : public IModule
{
public:
  DebugModule();
  ~DebugModule();
  virtual bool Start(IllumoContext* context) override;
  void Update(double dt) override;
  void DispatchDrawables(Scene* scene) override;
  void Exit() override;

private:
  bool isShowFpsEnabled() const;
  void updateFpsCounter(double dt);
  void updateWatermarkPosition();
  void registerRendererCommands();
  void unregisterRendererCommands();
  void createRendererDemo();

  GLString* fpsLabel;
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
  double fpsAccum;
  int fpsFrames;
  int fpsDisplay;
};
