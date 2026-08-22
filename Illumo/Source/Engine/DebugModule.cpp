#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include "Rendering/PresentationTiming.h"
#include <GLFW/glfw3.h>
#include <Illumo/Engine/DebugModule.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Services/Logger.h>
#include <tracy/Tracy.hpp>

DebugModule::DebugModule()
  : fpsLabel(nullptr)
  , rendererDemo(nullptr)
  , animatedSpriteIndex(0)
  , rotatingSpriteIndex(0)
  , rendererDemoEnabled(false)
  , rendererDemoRotation(0.0)
  , fpsAccum(0.0)
  , fpsFrames(0)
  , fpsDisplay(0)
{
}

DebugModule::~DebugModule()
{
  Exit();
}

bool
DebugModule::Start(IllumoContext* context)
{
  // D-E5: fail loud if the frozen service bag is incomplete.
  if (!IllumoContextHasDebugCore(context)) {
    Logger::LogError(
      "DebugModule::Start: IllumoContext missing required services "
      "(envVars, window, renderer, inputManager, commandLine, "
      "commandRegistry, assetManager)");
    ic = context;
    return false;
  }
  ic = context;

  // Required for GLString / SplashText screen-space drawing
  GLString::setRenderWindow(ic->window);

  fpsLabel =
    new GLString("FPS: 0", 80, 255, 120, 255, 18, 12, 12, ic->renderer);
  fpsLabel->setPanelStyle(UiTheme::statusPanel());
  fpsLabel->setVisible(isShowFpsEnabled());
  createRendererDemo();
  registerRendererCommands();

  return true;
}

void
DebugModule::createRendererDemo()
{
  rendererDemoTexture =
    ic->assetManager->acquireTexture("Assets/RendererDemo/showcase-atlas.ppm",
                                     TextureOptions{},
                                     AssetLoadMode::Async);
  ShaderPaths demoShaderPaths;
  demoShaderPaths.vertexPath = "Assets/RendererDemo/showcase-sprite.vert";
  demoShaderPaths.fragmentPath = "Assets/RendererDemo/showcase-sprite.frag";
  rendererDemoShader =
    ic->assetManager->acquireShader(demoShaderPaths, AssetLoadMode::Async);
  RenderStyle demoStyle;
  demoStyle.shaderHandle = rendererDemoShader;
  demoStyle.pipeline.depthTestEnabled = false;
  demoStyle.pipeline.blendEnabled = true;
  demoStyle.pipeline.blendSrc = BlendFactor::SrcAlpha;
  demoStyle.pipeline.blendDst = BlendFactor::OneMinusSrcAlpha;
  demoStyle.pipeline.faceCullingEnabled = false;
  demoStyle.pipeline.primitives = Primitives::Triangles;
  demoStyle.ready = rendererDemoShader.isValid();
  rendererDemoStyle = ic->renderer->createStyle(demoStyle);

  rendererDemo = new GameVisual();
  rendererDemo->setWindow(ic->window);
  rendererDemo->setCamera(ic->camera);
  rendererDemo->setSpace(PrimitiveSpace::Pixels);
  rendererDemo->prepare(ic->renderer);

  ColorRgba panel{ 15, 22, 38, 220 };
  ColorRgba border{ 110, 190, 255, 255 };
  size_t panelIndex =
    rendererDemo->addFilledRect(24.0f, 64.0f, 360.0f, 180.0f, panel);
  size_t borderIndex =
    rendererDemo->addOutlineRect(24.0f, 64.0f, 360.0f, 180.0f, border, 2.0f);
  rendererDemo->getShape(panelIndex)->drawOrder = -100;
  rendererDemo->getShape(borderIndex)->drawOrder = 100;

  TextureRegion redRegion = TextureRegion::gridCell(4, 1, 0, 0);
  TextureRegion greenRegion = TextureRegion::gridCell(4, 1, 1, 0);
  size_t backSprite =
    rendererDemo->addCenteredSprite(rendererDemoTexture,
                                    120.0f,
                                    150.0f,
                                    100.0f,
                                    100.0f,
                                    redRegion,
                                    ColorRgba{ 255, 255, 255, 180 });
  rendererDemo->getSprite(backSprite)->drawOrder = 0;
  rendererDemo->getSprite(backSprite)->styleHandle = rendererDemoStyle;
  rendererDemo->getSprite(backSprite)->transform.scaleX = 1.25f;

  rotatingSpriteIndex =
    rendererDemo->addCenteredSprite(rendererDemoTexture,
                                    168.0f,
                                    150.0f,
                                    96.0f,
                                    96.0f,
                                    greenRegion,
                                    ColorRgba{ 255, 220, 255, 190 });
  SpritePrimitive* rotating = rendererDemo->getSprite(rotatingSpriteIndex);
  rotating->drawOrder = 1;
  rotating->styleHandle = rendererDemoStyle;
  rotating->flipX = true;

  animatedSpriteIndex =
    rendererDemo->addCenteredSprite(rendererDemoTexture,
                                    300.0f,
                                    150.0f,
                                    96.0f,
                                    96.0f,
                                    redRegion,
                                    ColorRgba{ 210, 255, 255, 255 });
  rendererDemo->getSprite(animatedSpriteIndex)->drawOrder = 2;
  rendererDemo->getSprite(animatedSpriteIndex)->styleHandle = rendererDemoStyle;

  for (unsigned int frame = 0; frame < 4; ++frame) {
    SpriteAnimationFrame animationFrame;
    animationFrame.region = TextureRegion::gridCell(4, 1, frame, 0);
    animationFrame.durationSeconds = 0.18;
    rendererDemoClip.frames.push_back(animationFrame);
  }
  rendererDemoClip.loopMode = SpriteLoopMode::PingPong;
  rendererDemoAnimator.setClip(&rendererDemoClip);
  rendererDemo->setVisible(false);
}

void
DebugModule::registerRendererCommands()
{
  ic->commandRegistry->RegisterCommand(
    "renderer_demo",
    [this](const std::vector<std::string>& args) {
      if (args.size() > 1 ||
          (!args.empty() && args[0] != "on" && args[0] != "off")) {
        ic->commandLine->logError("Usage: renderer_demo [on|off]");
        return;
      }
      if (!args.empty()) {
        rendererDemoEnabled = args[0] == "on";
      }
      if (rendererDemo != nullptr) {
        rendererDemo->setVisible(rendererDemoEnabled);
      }
      ic->commandLine->logSuccess(std::string("Renderer demo: ") +
                                  (rendererDemoEnabled ? "on" : "off"));
    },
    "renderer_demo [on|off]",
    "Show or hide the reusable 2D renderer showcase",
    { "on", "off" });

  ic->commandRegistry->RegisterCommand(
    "assets",
    [this](const std::vector<std::string>& args) {
      if (!args.empty()) {
        ic->commandLine->logError("Usage: assets");
        return;
      }
      const std::vector<std::string> descriptions =
        ic->assetManager->describeAssets();
      if (descriptions.empty()) {
        ic->commandLine->logNormal("No managed assets");
      }
      for (const std::string& description : descriptions) {
        ic->commandLine->logNormal(description);
      }
    },
    "assets",
    "List managed texture/shader state, references, revisions, and errors");

  ic->commandRegistry->RegisterCommand(
    "asset_reload",
    [this](const std::vector<std::string>& args) {
      if (args.size() != 1) {
        ic->commandLine->logError("Usage: asset_reload <all|path>");
        return;
      }
      const size_t queued = args[0] == "all"
                              ? ic->assetManager->reloadAll()
                              : ic->assetManager->reload(args[0]);
      ic->commandLine->logNormal("Asset reloads queued: " +
                                 std::to_string(queued));
    },
    "asset_reload <all|path>",
    "Queue explicit texture/shader reloads",
    { "all" });
}

void
DebugModule::unregisterRendererCommands()
{
  if (ic == nullptr || ic->commandRegistry == nullptr) {
    return;
  }
  ic->commandRegistry->UnregisterCommand("renderer_demo");
  ic->commandRegistry->UnregisterCommand("assets");
  ic->commandRegistry->UnregisterCommand("asset_reload");
}

bool
DebugModule::isShowFpsEnabled() const
{
  if (!ic || !ic->envVars) {
    return false;
  }
  return ic->envVars->getVar("showFPS").valueAsBool;
}

void
DebugModule::updateFpsCounter(double dt)
{
  if (!isShowFpsEnabled() || !fpsLabel) {
    if (fpsLabel) {
      fpsLabel->setVisible(false);
    }
    return;
  }

  fpsLabel->setVisible(true);
  fpsFrames += 1;
  fpsAccum += dt;

  // Refresh displayed FPS about once per second (stable, readable)
  if (fpsAccum >= 1.0) {
    fpsDisplay =
      static_cast<int>(static_cast<double>(fpsFrames) / fpsAccum + 0.5);
    fpsFrames = 0;
    fpsAccum = 0.0;

    const IBackend* backend = ic->renderer->getBackend();
    const int pacedFps = backend != nullptr ? backend->getFPS() : 0;
    fpsLabel->setContent(
      buildFrameRateLabel(ic->window->isFramePaced(), pacedFps, fpsDisplay));
  }
}

void
DebugModule::Update(double dt)
{
  ZoneNamed(DebugModuleUpdateZone, "DebugModule Update");

  // Host erases modules that fail Start; still guard for incomplete fixtures.
  if (ic == nullptr || ic->inputManager == nullptr ||
      ic->commandLine == nullptr) {
    return;
  }

  updateFpsCounter(dt);
  if (rendererDemoEnabled && rendererDemo != nullptr) {
    rendererDemoAnimator.update(dt);
    rendererDemoRotation += dt * 0.8;
    SpritePrimitive* animated = rendererDemo->getSprite(animatedSpriteIndex);
    SpritePrimitive* rotating = rendererDemo->getSprite(rotatingSpriteIndex);
    if (animated != nullptr) {
      animated->region = rendererDemoAnimator.currentRegion();
    }
    if (rotating != nullptr) {
      rotating->transform.rotationRadians =
        static_cast<float>(rendererDemoRotation);
    }
  }

  // 1. Process Key Queue from InputManager
  auto& keyQueue = ic->inputManager->getKeyQueue();
  while (!keyQueue.empty()) {
    InputManager::KeyPressEvent event = keyQueue.front();
    keyQueue.pop();

    KeyCode key = event.key;
    InputAction action = event.action;
    bool controlPressed = (event.modifiers & GLFW_MOD_CONTROL) != 0;
    bool shiftPressed = (event.modifiers & GLFW_MOD_SHIFT) != 0;

    // Toggle CommandLine with grave accent / tilde key
    if (key == KeyCode::Grave && action == InputAction::Press) {
      ic->commandLine->Toggle();
      // Clear character queue when toggling to avoid tilde character being
      // typed
      ic->inputManager->clearCharQueue();
      continue;
    }

    if (ic->commandLine->isOpen) {
      if (action == InputAction::Press || action == InputAction::Hold) {
        if (key == KeyCode::Backspace) {
          ic->commandLine->HandleBackspace(controlPressed);
        } else if (key == KeyCode::Delete) {
          ic->commandLine->HandleDelete(controlPressed);
        } else if (key == KeyCode::Left) {
          ic->commandLine->MoveCursorLeft(controlPressed, shiftPressed);
        } else if (key == KeyCode::Right) {
          ic->commandLine->MoveCursorRight(controlPressed, shiftPressed);
        } else if (key == KeyCode::Home) {
          ic->commandLine->MoveCursorHome(shiftPressed);
        } else if (key == KeyCode::End) {
          ic->commandLine->MoveCursorEnd(shiftPressed);
        } else if (key == KeyCode::Tab) {
          ic->commandLine->Complete();
        } else if (controlPressed && key == KeyCode::A) {
          ic->commandLine->SelectAll();
        } else if (controlPressed && key == KeyCode::L) {
          ic->commandLine->ClearInput();
        } else if (key == KeyCode::Enter) {
          ic->commandLine->ExecuteCommand();
        } else if (key == KeyCode::Escape) {
          ic->commandLine->Toggle();
        } else if (key == KeyCode::Up) {
          ic->commandLine->HistoryUp();
        } else if (key == KeyCode::Down) {
          ic->commandLine->HistoryDown();
        } else if (key == KeyCode::PageUp) {
          ic->commandLine->ScrollUp();
        } else if (key == KeyCode::PageDown) {
          ic->commandLine->ScrollDown();
        }
      }
    } else {
      // Close window on Escape when console is closed. Q is owned by the
      // product module so it can confirm before requesting shutdown.
      if (key == KeyCode::Escape && action == InputAction::Press)
        ic->window->requestClose();
    }
  }

  // 2. Process Character Queue from InputManager
  auto& charQueue = ic->inputManager->getCharQueue();
  while (!charQueue.empty()) {
    unsigned int codepoint = charQueue.front();
    charQueue.pop();

    if (ic->commandLine->isOpen) {
      if (codepoint != '`' && codepoint != '~') {
        ic->commandLine->AddCharacter(codepoint);
      }
    }
  }

  // 3. Process Mouse Input for CommandLine
  if (ic->commandLine && ic->commandLine->isOpen && ic->inputManager) {
    double* scrollOffsetPtr = ic->inputManager->getMouseScrollOffset();
    if (scrollOffsetPtr && *scrollOffsetPtr != 0.0) {
      ic->commandLine->HandleScroll(*scrollOffsetPtr);
      *scrollOffsetPtr = 0.0;
    }

    std::array<double, 2> mouseCoords = ic->inputManager->getMousePosition();
    bool isLeftPressed =
      ic->inputManager->isMouseButtonPressed(KeyCode::MouseLeft);
    bool isLeftReleased =
      ic->inputManager->isMouseButtonReleased(KeyCode::MouseLeft);

    static bool wasMouseLeftPressed = false;
    if (isLeftPressed) {
      if (!wasMouseLeftPressed) {
        ic->commandLine->HandleMousePress(mouseCoords[0], mouseCoords[1]);
        wasMouseLeftPressed = true;
      } else {
        ic->commandLine->HandleMouseDrag(mouseCoords[0], mouseCoords[1]);
      }
    } else {
      if (wasMouseLeftPressed || isLeftReleased) {
        ic->commandLine->HandleMouseRelease();
        wasMouseLeftPressed = false;
      }
    }
  }

  // 4. Execute command queue
  if (ic->commandRegistry != nullptr) {
    ic->commandRegistry->ExecuteQueue();
  }
}

void
DebugModule::Exit()
{
  unregisterRendererCommands();
  if (rendererDemo != nullptr) {
    delete rendererDemo;
    rendererDemo = nullptr;
  }
  if (ic != nullptr && ic->renderer != nullptr && rendererDemoStyle.isValid()) {
    ic->renderer->destroyStyle(rendererDemoStyle);
    rendererDemoStyle = RenderStyleHandle{};
  }
  if (ic != nullptr && ic->assetManager != nullptr &&
      rendererDemoTexture.isValid()) {
    ic->assetManager->releaseTexture(rendererDemoTexture);
    rendererDemoTexture = TextureHandle{};
  }
  if (ic != nullptr && ic->assetManager != nullptr &&
      rendererDemoShader.isValid()) {
    ic->assetManager->releaseShader(rendererDemoShader);
    rendererDemoShader = ShaderHandle{};
  }
  if (fpsLabel) {
    delete fpsLabel;
    fpsLabel = nullptr;
  }
}

void
DebugModule::DispatchDrawables(Scene* scene)
{
  if (ic == nullptr || scene == nullptr) {
    return;
  }
  // Skip fully closed console (no anim) — avoids chrono/lerp + empty token
  // work. Owners rebuild GameVisual primitives inside AppendCommands.
  if (ic->commandLine && ic->commandLine->wantsDraw()) {
    scene->AddDrawable(ic->commandLine, RenderLayerId::UI);
  }
  if (fpsLabel && isShowFpsEnabled()) {
    scene->AddDrawable(fpsLabel, RenderLayerId::Debug);
  }
  if (rendererDemo != nullptr && rendererDemoEnabled) {
    scene->AddDrawable(rendererDemo, RenderLayerId::Debug);
  }
}
