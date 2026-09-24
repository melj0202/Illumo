#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include "DebugOverlayState.h"
#include "FileTreeOverlay.h"
#include "Platform/PixelWindow.h"
#include "ProfilerOverlay.h"
#include <GLFW/glfw3.h>
#include <Illumo/Engine/DebugModule.h>
#include <Illumo/Rendering/Font.h>
#include <Illumo/Rendering/Primitives/SoftwareCanvas.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <queue>
#include <string>
#include <tracy/Tracy.hpp>
#include <vector>

DebugModule::DebugModule(FrameProfiler* profiler)
  : m_profiler(profiler)
  , diagnosticsLabel(nullptr)
  , diagnostics(std::make_unique<DebugOverlayState>())
  , watermarkLabel(nullptr)
  , rendererDemo(nullptr)
  , animatedSpriteIndex(0)
  , rotatingSpriteIndex(0)
  , rendererDemoEnabled(false)
  , rendererDemoRotation(0.0)
{
}

DebugModule::~DebugModule() = default;

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

  if (m_profiler != nullptr) {
    m_profilerOverlay = std::make_unique<ProfilerOverlay>(*m_profiler);
    m_profilerOverlay->prepare(ic->renderer, ic->window, ic->camera);
  }
  m_fileTreeOverlay = std::make_unique<FileTreeOverlay>();
  m_fileTreeOverlay->prepare(ic->renderer, ic->window, ic->camera);

  // Required for GLString / SplashText screen-space drawing
  GLString::setRenderWindow(ic->window);

  diagnosticsLabel =
    new GLString("", 80, 255, 120, 255, 18, 12, 12, ic->renderer);
  diagnosticsLabel->setPanelStyle(UiTheme::statusPanel());
  *diagnostics = DebugOverlayState{};
  updateDiagnostics(0.0);

  // Translucent watermark in bottom-right corner for debug compilation builds
  watermarkLabel =
    new GLString("development build", 245, 80, 80, 140, 18, 0, 0, ic->renderer);
  updateWatermarkPosition();
  watermarkLabel->setVisible(true);

  createRendererDemo();
  registerRendererCommands();
  // The console may pop out into its own window where the platform can
  // present one (D-UI5).
  ic->commandLine->setDetachAvailable(PixelWindow::isPresentationSupported());

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
  if (m_profilerOverlay != nullptr) {
    ic->commandRegistry->RegisterCommand(
      "profiler",
      [this](const std::vector<std::string>& args) {
        if (args.size() > 1 || (!args.empty() && args[0] != "on" &&
                                args[0] != "off" && args[0] != "toggle")) {
          ic->commandLine->logError("Usage: profiler [on|off|toggle]");
          return;
        }
        if (!args.empty()) {
          const bool enabled =
            args[0] == "toggle" ? !m_profiler->enabled() : args[0] == "on";
          m_profilerOverlay->setEnabled(enabled);
        }
        ic->commandLine->logSuccess(std::string("Frame profiler: ") +
                                    (m_profiler->enabled() ? "on" : "off"));
      },
      "profiler [on|off|toggle]",
      "Main-thread timing pie; F6 toggles, 1-3 drill down, 0 returns",
      { "on", "off", "toggle" });
  }
  ic->commandRegistry->RegisterCommand(
    "files",
    [this](const std::vector<std::string>& args) {
      if (args.size() > 1) {
        ic->commandLine->logError("Usage: files [path|off]");
        return;
      }
      if (!args.empty() && args[0] == "off") {
        m_fileTreeOverlay->hide();
        return;
      }
      if (ic->fileTree == nullptr) {
        ic->commandLine->logError("No file tree is mounted in this host");
        return;
      }
      const std::string root = args.empty() ? std::string("/") : args[0];
      FileTreeStatus status;
      // The synthesized root always lists; any other root must be a
      // directory in the tree.
      if (root != "/" &&
          (!ic->fileTree->stat(root, status) || !status.directory)) {
        ic->commandLine->logError("Not a directory: " + root);
        return;
      }
      m_fileTreeOverlay->show(ic->fileTree, root);
      ic->commandLine->logSuccess("Browsing " + root +
                                  " (close the console to navigate)");
    },
    "files [path|off]",
    "Browse the mounted file tree; arrows navigate, Escape closes",
    { "off", "/", "/app", "/engine", "/packages", "/project" });
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
  ic->commandRegistry->UnregisterCommand("files");
  if (m_profiler != nullptr) {
    ic->commandRegistry->UnregisterCommand("profiler");
  }
  ic->commandRegistry->UnregisterCommand("assets");
  ic->commandRegistry->UnregisterCommand("asset_reload");
}

void
DebugModule::updateDiagnostics(double dt)
{
  if (diagnosticsLabel == nullptr || ic == nullptr || ic->envVars == nullptr) {
    return;
  }
  const IBackend* backend = ic->renderer->getBackend();
  if (diagnostics->update(dt,
                          ic->envVars->getVar("showFPS").valueAsBool,
                          ic->envVars->getVar("showMemory").valueAsBool,
                          ic->window->isFramePaced(),
                          backend != nullptr ? backend->getFPS() : 0)) {
    diagnosticsLabel->setContent(diagnostics->content());
  }
  diagnosticsLabel->setVisible(diagnostics->visible());
}

void
DebugModule::updateWatermarkPosition()
{
  if (!watermarkLabel || !ic || !ic->window) {
    return;
  }
  const std::array<int, 2> dimensions = ic->window->getWindowDimensions();
  const float windowWidth = static_cast<float>(dimensions[0]);
  const float windowHeight = static_cast<float>(dimensions[1]);
  const float uiScale =
    ic->renderer != nullptr ? ic->renderer->getUiScale() : 1.0f;
  const float resX = (uiScale > 0.0f) ? (windowWidth / uiScale) : windowWidth;
  const float resY = (uiScale > 0.0f) ? (windowHeight / uiScale) : windowHeight;

  std::shared_ptr<Font> font = Font::getDefaultFont();
  const float sizePt = static_cast<float>(watermarkLabel->getSize());
  const float textWidth =
    font ? font->measureText(watermarkLabel->getContent(), sizePt).width
         : (static_cast<float>(watermarkLabel->getContent().size()) * sizePt *
            0.6f);

  const int posX = static_cast<int>(std::max(0.0f, resX - textWidth - 14.0f));
  const int posY = static_cast<int>(std::max(0.0f, resY - sizePt - 12.0f));
  watermarkLabel->setX(posX);
  watermarkLabel->setY(posY);
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

  updateDiagnostics(dt);
  updateWatermarkPosition();
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

  // Overlay input runs before the required product module. Consume Grave and
  // open-console editing globally; leave remaining events for the product.
  std::queue<InputManager::KeyPressEvent>& keyQueue =
    ic->inputManager->getKeyQueue();
  std::queue<InputManager::KeyPressEvent> remainingKeys;
  while (!keyQueue.empty()) {
    InputManager::KeyPressEvent event = keyQueue.front();
    keyQueue.pop();

    KeyCode key = event.key;
    InputAction action = event.action;

    if (m_profilerOverlay != nullptr &&
        m_profilerOverlay->handleKey(
          key, action, ic->commandLine->isOpen, event.modifiers != 0)) {
      ic->inputManager->suppressKeyForFrame(key);
      continue;
    }
    if (m_fileTreeOverlay != nullptr && event.modifiers == 0 &&
        m_fileTreeOverlay->handleKey(key, action, ic->commandLine->isOpen)) {
      ic->inputManager->suppressKeyForFrame(key);
      continue;
    }

    if (key == KeyCode::Grave && action == InputAction::Press) {
      // With the console in its own window, the console key sends it back
      // into the game, closed.
      if (ic->commandLine->isDetached()) {
        closeDetachedConsole(false);
      } else {
        ic->commandLine->Toggle();
      }
      ic->inputManager->clearCharQueue();
      continue;
    }

    // Console `bind` targets F1-F12 and works whether or not the console is
    // open. Unbound keys fall through to the console or the product.
    if (key >= KeyCode::F1 && key <= KeyCode::F12) {
      const std::string keyName =
        "F" + std::to_string(static_cast<int>(key) -
                             static_cast<int>(KeyCode::F1) + 1);
      if (ic->commandLine->HasKeyBinding(keyName)) {
        if (action == InputAction::Press) {
          ic->commandLine->RunKeyBinding(keyName);
        }
        ic->inputManager->suppressKeyForFrame(key);
        continue;
      }
    }

    if (!ic->commandLine->isOpen) {
      remainingKeys.push(event);
      continue;
    }

    routeConsoleKey(key, action, event.modifiers);
  }
  keyQueue.swap(remainingKeys);

  if (m_profilerOverlay != nullptr) {
    m_profilerOverlay->captureInput(*ic->inputManager, ic->commandLine->isOpen);
    const std::array<int, 2> dimensions = ic->window->getWindowDimensions();
    const float scale = ic->renderer->getUiScale();
    m_profilerOverlay->update(dt,
                              static_cast<float>(dimensions[0]) / scale,
                              static_cast<float>(dimensions[1]) / scale);
  }

  if (m_fileTreeOverlay != nullptr && m_fileTreeOverlay->visible()) {
    // A tree that was withdrawn (the product exited) closes the browser.
    if (ic->fileTree == nullptr) {
      m_fileTreeOverlay->hide();
    } else if (!ic->commandLine->isOpen) {
      double* scroll = ic->inputManager->getMouseScrollOffset();
      if (scroll != nullptr && m_fileTreeOverlay->scroll(*scroll)) {
        *scroll = 0.0;
      }
    }
    const std::array<int, 2> dimensions = ic->window->getWindowDimensions();
    const float scale = ic->renderer->getUiScale();
    m_fileTreeOverlay->update(static_cast<float>(dimensions[0]) / scale,
                              static_cast<float>(dimensions[1]) / scale);
  }

  std::queue<unsigned int>& charQueue = ic->inputManager->getCharQueue();
  if (ic->commandLine->isOpen) {
    while (!charQueue.empty()) {
      unsigned int codepoint = charQueue.front();
      charQueue.pop();
      if (codepoint != '`' && codepoint != '~') {
        ic->commandLine->AddCharacter(codepoint);
      }
    }
  }

  // Mouse input for the in-game console.
  if (ic->commandLine->isOpen) {
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

    if (isLeftPressed) {
      if (!m_consoleMouseDown) {
        ic->commandLine->HandleMousePress(mouseCoords[0], mouseCoords[1]);
        m_consoleMouseDown = true;
      } else {
        ic->commandLine->HandleMouseDrag(mouseCoords[0], mouseCoords[1]);
      }
    } else if (m_consoleMouseDown || isLeftReleased) {
      ic->commandLine->HandleMouseRelease();
      m_consoleMouseDown = false;
    }
  } else {
    // A drag that tore the console out ends here; the next open starts clean.
    m_consoleMouseDown = false;
  }

  processConsoleWindowRequest();
  updateDetachedConsole();
  processConsoleWindowRequest();

  // Execute command queue
  if (ic->commandRegistry != nullptr) {
    ic->commandRegistry->ExecuteQueue();
  }
}

void
DebugModule::routeConsoleKey(KeyCode key, InputAction action, int modifiers)
{
  const bool controlPressed = (modifiers & GLFW_MOD_CONTROL) != 0;
  const bool shiftPressed = (modifiers & GLFW_MOD_SHIFT) != 0;
  if (action == InputAction::Press || action == InputAction::Hold) {
    if (key == KeyCode::Backspace) {
      ic->commandLine->HandleBackspace(controlPressed);
    } else if (key == KeyCode::Delete) {
      ic->commandLine->HandleDelete(controlPressed);
    } else if (key == KeyCode::Left) {
      ic->commandLine->MoveCursorLeft(controlPressed, shiftPressed);
    } else if (key == KeyCode::Right) {
      ic->commandLine->MoveCursorRight(controlPressed, shiftPressed);
    } else if (controlPressed && key == KeyCode::Home) {
      ic->commandLine->ScrollToTop();
    } else if (controlPressed && key == KeyCode::End) {
      ic->commandLine->ScrollToBottom();
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
    } else if (controlPressed && key == KeyCode::C) {
      ic->commandLine->CopySelection();
    } else if (controlPressed && key == KeyCode::X) {
      ic->commandLine->CutSelection();
    } else if (controlPressed && key == KeyCode::V) {
      ic->commandLine->Paste();
    } else if (key == KeyCode::Enter) {
      ic->commandLine->ExecuteCommand();
    } else if (controlPressed && key == KeyCode::R) {
      ic->commandLine->BeginReverseSearch();
    } else if (key == KeyCode::Escape) {
      // Key repeat must not re-toggle the console it just closed; Escape
      // first leaves a history search. A detached window closes only
      // through its own controls, so a stray Escape cannot lose it.
      if (ic->commandLine->isReverseSearchActive()) {
        ic->commandLine->CancelReverseSearch();
      } else if (action == InputAction::Press &&
                 !ic->commandLine->isDetached()) {
        ic->commandLine->Toggle();
      }
    } else if (controlPressed && key == KeyCode::Up) {
      ic->commandLine->ScrollUp();
    } else if (controlPressed && key == KeyCode::Down) {
      ic->commandLine->ScrollDown();
    } else if (key == KeyCode::Up) {
      ic->commandLine->HistoryUp();
    } else if (key == KeyCode::Down) {
      ic->commandLine->HistoryDown();
    } else if (key == KeyCode::PageUp) {
      ic->commandLine->ScrollPageUp();
    } else if (key == KeyCode::PageDown) {
      ic->commandLine->ScrollPageDown();
    }
  }
}

void
DebugModule::processConsoleWindowRequest()
{
  const CommandLine::WindowRequest request =
    ic->commandLine->takeWindowRequest();
  switch (request.kind) {
    case CommandLine::WindowRequestKind::Detach:
      detachConsole(
        request.originX, request.originY, request.width, request.height);
      break;
    case CommandLine::WindowRequestKind::Dock:
      closeDetachedConsole(true);
      break;
    case CommandLine::WindowRequestKind::Close:
      closeDetachedConsole(false);
      break;
    case CommandLine::WindowRequestKind::None:
    default:
      break;
  }
}

void
DebugModule::detachConsole(int originX, int originY, int width, int height)
{
  if (m_consoleWindow != nullptr) {
    m_consoleWindow->focus();
    return;
  }
  // The request is relative to the game window's client area; place the
  // new window's client area at the same spot on screen.
  int windowX = 0;
  int windowY = 0;
  GLFWwindow* mainWindow = ic->window->getWindowInstance();
  if (mainWindow != nullptr) {
    glfwGetWindowPos(mainWindow, &windowX, &windowY);
  }
  const int clientWidth = std::max(480, width);
  const int clientHeight = std::max(280, height);
  std::string error;
  m_consoleWindow =
    PixelWindow::create(ic->commandLine->getApplicationName() + " console",
                        windowX + originX,
                        std::max(windowY + originY, 32),
                        clientWidth,
                        clientHeight,
                        &error);
  if (m_consoleWindow == nullptr) {
    ic->commandLine->logError("Could not pop out the console: " + error);
    return;
  }
  if (m_consoleCanvas == nullptr) {
    m_consoleCanvas = std::make_unique<SoftwareCanvas>();
  }
  int actualWidth = clientWidth;
  int actualHeight = clientHeight;
  m_consoleWindow->clientSize(&actualWidth, &actualHeight);
  m_detachedMouseDown = false;
  ic->commandLine->setDetached(true, actualWidth, actualHeight);
  ic->inputManager->clearCharQueue();
}

void
DebugModule::closeDetachedConsole(bool reopenInGame)
{
  if (m_consoleWindow == nullptr && !ic->commandLine->isDetached()) {
    return;
  }
  m_consoleWindow.reset();
  m_detachedMouseDown = false;
  ic->commandLine->setDetached(false, 0, 0);
  if (reopenInGame && !ic->commandLine->isOpen) {
    ic->commandLine->Toggle();
  }
  GLFWwindow* mainWindow = ic->window->getWindowInstance();
  if (mainWindow != nullptr) {
    glfwFocusWindow(mainWindow);
  }
}

void
DebugModule::updateDetachedConsole()
{
  if (m_consoleWindow == nullptr) {
    return;
  }
  if (m_consoleWindow->isCloseRequested()) {
    closeDetachedConsole(false);
    return;
  }
  CommandLine* console = ic->commandLine;
  const std::vector<PixelWindow::Event> events = m_consoleWindow->takeEvents();
  for (const PixelWindow::Event& event : events) {
    if (m_consoleWindow == nullptr || !console->isDetached()) {
      break;
    }
    switch (event.kind) {
      case PixelWindow::EventKind::Key:
        if (event.key == KeyCode::Grave && event.action == InputAction::Press) {
          closeDetachedConsole(false);
        } else if (event.key >= KeyCode::F1 && event.key <= KeyCode::F12) {
          const std::string keyName =
            "F" + std::to_string(static_cast<int>(event.key) -
                                 static_cast<int>(KeyCode::F1) + 1);
          if (event.action == InputAction::Press &&
              console->HasKeyBinding(keyName)) {
            console->RunKeyBinding(keyName);
          }
        } else {
          routeConsoleKey(event.key, event.action, event.modifiers);
        }
        break;
      case PixelWindow::EventKind::Character:
        if (event.codepoint != '`' && event.codepoint != '~') {
          console->AddCharacter(event.codepoint);
        }
        break;
      case PixelWindow::EventKind::MouseButton:
        if (event.key != KeyCode::MouseLeft) {
          break;
        }
        if (event.action == InputAction::Press) {
          m_detachedMouseDown = true;
          console->HandleMousePress(event.x, event.y);
        } else if (event.action == InputAction::Release) {
          m_detachedMouseDown = false;
          console->HandleMouseRelease();
        }
        break;
      case PixelWindow::EventKind::MouseMove:
        if (m_detachedMouseDown) {
          console->HandleMouseDrag(event.x, event.y);
        }
        break;
      case PixelWindow::EventKind::Scroll:
        console->HandleScroll(event.scroll);
        break;
      default:
        break;
    }
  }
  if (m_consoleWindow == nullptr || !console->isDetached()) {
    return;
  }

  int width = 0;
  int height = 0;
  m_consoleWindow->clientSize(&width, &height);
  if (width <= 0 || height <= 0) {
    // Minimized: nothing to draw until it is restored.
    return;
  }
  console->setDetachedSize(width, height);
  const bool resized =
    m_consoleCanvas->width() != width || m_consoleCanvas->height() != height;
  if (resized) {
    m_consoleCanvas->resize(width, height);
  }
  const bool rebuilt = console->ComposeDetached();
  if (rebuilt || resized) {
    m_consoleCanvas->clear(ColorRgba{ 14, 14, 14, 255 });
    m_consoleCanvas->draw(console->getVisual());
    m_consoleWindow->takeRepaintRequest();
    m_consoleWindow->present(m_consoleCanvas->pixels(), width, height);
  } else if (m_consoleWindow->takeRepaintRequest()) {
    m_consoleWindow->present(m_consoleCanvas->pixels(), width, height);
  }
}

void
DebugModule::Exit()
{
  if (ic != nullptr && ic->commandLine != nullptr) {
    closeDetachedConsole(false);
    ic->commandLine->setDetachAvailable(false);
  }
  m_consoleWindow.reset();
  m_consoleCanvas.reset();
  unregisterRendererCommands();
  m_profilerOverlay.reset();
  m_fileTreeOverlay.reset();
  if (m_profiler != nullptr) {
    m_profiler->setEnabled(false);
  }
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
  if (diagnosticsLabel) {
    delete diagnosticsLabel;
    diagnosticsLabel = nullptr;
  }
  if (watermarkLabel) {
    delete watermarkLabel;
    watermarkLabel = nullptr;
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
  if (diagnosticsLabel && diagnosticsLabel->isVisible()) {
    scene->AddDrawable(diagnosticsLabel, RenderLayerId::Debug);
  }
  if (m_profilerOverlay != nullptr && m_profiler->enabled()) {
    scene->AddDrawable(&m_profilerOverlay->visual(), RenderLayerId::Debug);
  }
  if (m_fileTreeOverlay != nullptr && m_fileTreeOverlay->visible()) {
    scene->AddDrawable(&m_fileTreeOverlay->visual(), RenderLayerId::Debug);
  }
  if (watermarkLabel && watermarkLabel->isVisible()) {
    scene->AddDrawable(watermarkLabel, RenderLayerId::Debug);
  }
  if (rendererDemo != nullptr && rendererDemoEnabled) {
    scene->AddDrawable(rendererDemo, RenderLayerId::Debug);
  }
}
