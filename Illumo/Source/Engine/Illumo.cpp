#include <Illumo/Engine/Illumo.h>

#include "Rendering/BackendConfig.h"
#include "Rendering/OpenGL/CreateOpenGLBackend.h"
#include "Rendering/RenderWindow.h"
#include "Rendering/Vulkan/CreateVulkanBackend.h"
#include <Illumo/Foundation/Profile.h>
#include <Illumo/Platform/PathText.h>
#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/DrawList.h>
#include <Illumo/Rendering/GLString.h>
#include <Illumo/Rendering/IBackend.h>
#include <Illumo/Rendering/RenderPass.h>
#include <Illumo/Rendering/RenderPipelineConfig.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Services/Logger.h>
#include <array>
#include <filesystem>
#include <glm/fwd.hpp>
#include <utility>

// The window size applyHostDefaults seeds as "WinX"/"WinY" (keep them in
// step), and the fallback for a saved size that cannot open a window.
static constexpr int kDefaultWindowWidth = 1280;
static constexpr int kDefaultWindowHeight = 720;

static void
cleanupBackendAfterInitializationFailure(
  std::unique_ptr<IBackend>* backend) noexcept
{
  if (backend == nullptr || !*backend) {
    return;
  }
  (*backend)->Shutdown();
  backend->reset();
}

static std::unique_ptr<IBackend>
createBackend(IRenderWindow* window, BackendDef api)
{
  if (api == BackendDef::VULKAN) {
    return CreateVulkanBackend(window);
  }
  return CreateOpenGLBackend(window);
}

static const char*
backendDisplayName(BackendDef api)
{
  return api == BackendDef::VULKAN ? "Vulkan" : "OpenGL";
}

Illumo::Illumo(IllumoConfig config)
  : m_applicationName(config.applicationName.empty() ? "Illumo"
                                                     : config.applicationName)
  , m_gpuCacheDirectory(config.gpuCacheDirectory)
  , m_windowFactory(CreateRenderWindowFor)
  , m_backendFactory(createBackend)
{
  std::filesystem::path environmentPath = EnvVars::ApplicationConfigPath();
  if (!config.environmentPath.empty() &&
      !pathFromUtf8(config.environmentPath, &environmentPath)) {
    Logger::LogWarning("The settings path is not valid UTF-8; using " +
                       pathToUtf8(environmentPath));
  }
  m_environment = std::make_unique<EnvVars>(environmentPath);
  applyHostDefaults();
  // The configured log level applies from here, before the console exists,
  // so window and GPU startup messages are not filtered by the fallback.
  Logger::setContext(m_environment.get(), nullptr);
}

Illumo::~Illumo()
{
  shutdown();
  Logger::setContext(nullptr, nullptr);
}

EnvVars&
Illumo::environment()
{
  return *m_environment;
}

const EnvVars&
Illumo::environment() const
{
  return *m_environment;
}

const std::string&
Illumo::applicationName() const
{
  return m_applicationName;
}

void
Illumo::applyHostDefaults()
{
  struct DefaultValue
  {
    const char* name;
    const char* value;
  };
  const DefaultValue defaults[] = {
    { "fps", "60" },
    { "vsync", "true" },
    { "WinX", "1280" },
    { "WinY", "720" },
    { "showFPS", "0" },
    { "showMemory", "0" },
    { "showGamepadVisuals", "0" },
    { "logLevel", "2" },
    { "fullscreen", "false" },
  };
  for (const DefaultValue& defaultValue : defaults) {
    if (m_environment->getVar(defaultValue.name).value.empty()) {
      m_environment->setVar(defaultValue.name, defaultValue.value);
    }
  }
  RenderPipelineConfig::seedDefaults(m_environment.get());
}

bool
Illumo::initialize()
{
  if (m_initialized) {
    Logger::LogWarning("Illumo::initialize called more than once; ignoring");
    return true;
  }
  ILLUMO_PROFILE_ZONE("Illumo.Initialize");

  int initialWindowWidth =
    static_cast<int>(m_environment->getVar("WinX").valueAsLong);
  int initialWindowHeight =
    static_cast<int>(m_environment->getVar("WinY").valueAsLong);
  // A settings file saved with a minimized window (0x0) or edited by hand
  // must not stop the window from opening.
  if (initialWindowWidth <= 0 || initialWindowHeight <= 0) {
    Logger::LogWarning("Ignoring the saved window size " +
                       std::to_string(initialWindowWidth) + "x" +
                       std::to_string(initialWindowHeight) + "; using " +
                       std::to_string(kDefaultWindowWidth) + "x" +
                       std::to_string(kDefaultWindowHeight));
    initialWindowWidth = kDefaultWindowWidth;
    initialWindowHeight = kDefaultWindowHeight;
    m_environment->setVar("WinX", initialWindowWidth);
    m_environment->setVar("WinY", initialWindowHeight);
  }
  BackendDef requestedApi = BackendDef::OPENGL;
  const std::string& requestedName = m_environment->getVar("GraphicsAPI").value;
  if (!requestedName.empty() &&
      !parseBackendDef(requestedName, &requestedApi)) {
    Logger::LogWarning("GraphicsAPI '" + requestedName +
                       "' names no rendering backend; using OpenGL");
  } else if (!isBackendImplemented(requestedApi)) {
    Logger::LogWarning("GraphicsAPI " + TokenToString(requestedApi) +
                       " is not available in this build; using OpenGL");
    requestedApi = BackendDef::OPENGL;
  }
  m_camera =
    std::make_unique<Camera>(glm::vec2(0.0f, 0.0f), 1.0f, m_environment.get());
  std::unique_ptr<IBackend> backend;
  bool started = startGraphics(
    requestedApi, initialWindowWidth, initialWindowHeight, &backend);
  if (!started && requestedApi != BackendDef::OPENGL) {
    // The preference stays saved, so a later launch tries it again.
    Logger::LogWarning(std::string(backendDisplayName(requestedApi)) +
                       " could not start; falling back to OpenGL");
    started = startGraphics(
      BackendDef::OPENGL, initialWindowWidth, initialWindowHeight, &backend);
  }
  if (!started) {
    releaseServices();
    return false;
  }
  {
    ILLUMO_PROFILE_ZONE("Illumo.CreateRenderer");
    m_renderer = std::make_unique<Renderer>(
      m_window.get(), m_environment.get(), m_camera.get(), std::move(backend));
    m_renderer->ensureBuiltinStyles();
  }
  Logger::LogTrace("Renderer ready with built-in styles");
  m_assetManager = std::make_unique<AssetManager>(m_renderer.get());
  Logger::LogTrace("Asset manager ready");
  m_commandRegistry = std::make_unique<CommandRegistry>();
  m_commandLine = std::make_unique<CommandLine>(m_environment.get(),
                                                m_commandRegistry.get(),
                                                m_window.get(),
                                                m_renderer.get(),
                                                m_applicationName);
  // Attaching the console replays everything logged so far into it.
  Logger::setContext(m_environment.get(), m_commandLine.get());
  Logger::LogTrace("Developer console attached");
  m_inputManager =
    std::make_unique<InputManager>(m_window->getWindowInstance());
  Logger::LogTrace("Input manager ready");
  m_scene = std::make_unique<DrawList>(m_window.get(), m_camera.get());
  m_pipelineConfigured = false;
  m_hdrFallbackWarned = false;
  GLString::setRenderWindow(m_window.get());

  m_context.envVars = m_environment.get();
  m_context.window = m_window.get();
  m_context.commandLine = m_commandLine.get();
  m_context.inputManager = m_inputManager.get();
  m_context.renderer = m_renderer.get();
  m_context.assetManager = m_assetManager.get();
  m_context.camera = m_camera.get();
  m_context.commandRegistry = m_commandRegistry.get();
  m_context.scene = m_scene.get();
  m_context.frameProfiler = &m_frameProfiler;
  m_initialized = true;
  Logger::LogInfo("Engine services initialized (renderer, assets, console, "
                  "input, scene)");
  return true;
}

bool
Illumo::startGraphics(BackendDef api,
                      int width,
                      int height,
                      std::unique_ptr<IBackend>* backend)
{
  {
    ILLUMO_PROFILE_ZONE("Illumo.CreateWindow");
    m_window = m_windowFactory(
      width, height, m_applicationName, m_environment.get(), api);
  }
  if (!m_window) {
    Logger::LogError(std::string("Illumo failed to create its render window "
                                 "for ") +
                     backendDisplayName(api));
    return false;
  }
  const std::array<int, 2> windowSize = m_window->getWindowDimensions();
  Logger::LogInfo("Render window ready: " + std::to_string(windowSize[0]) +
                  "x" + std::to_string(windowSize[1]) + ", monitor " +
                  std::to_string(m_window->getRefreshRate()) + " Hz");
  *backend = m_backendFactory(m_window.get(), api);
  if (!*backend) {
    Logger::LogError(std::string("Illumo failed to create its ") +
                     backendDisplayName(api) + " rendering backend");
    m_window.reset();
    return false;
  }
  if (!m_gpuCacheDirectory.empty()) {
    std::filesystem::path gpuCache;
    if (pathFromUtf8(m_gpuCacheDirectory, &gpuCache)) {
      (*backend)->setGpuCacheDirectory(gpuCache);
    }
  }
  bool backendInitialized = false;
  {
    ILLUMO_PROFILE_ZONE("Illumo.InitializeBackend");
    backendInitialized = (*backend)->Initialize();
  }
  if (!backendInitialized) {
    Logger::LogError(std::string("Illumo failed to initialize its ") +
                     backendDisplayName(api) + " rendering backend");
    cleanupBackendAfterInitializationFailure(backend);
    m_window.reset();
    return false;
  }
  Logger::LogInfo(std::string("Rendering backend: ") + backendDisplayName(api));
  return true;
}

void
Illumo::processGlobalHotkeys()
{
  if (m_inputManager == nullptr) {
    return;
  }

  // When console is open, do not intercept hotkeys so text editing is
  // unimpeded.
  if (m_commandLine != nullptr && m_commandLine->isOpen) {
    return;
  }

  std::queue<InputManager::KeyPressEvent>& keyQueue =
    m_inputManager->getKeyQueue();
  std::queue<InputManager::KeyPressEvent> remainingKeys;

  while (!keyQueue.empty()) {
    InputManager::KeyPressEvent event = keyQueue.front();
    keyQueue.pop();

    if (event.action == InputAction::Press) {
      if (event.key == KeyCode::F11) {
        if (m_window != nullptr) {
          m_window->toggleFullscreen();
          if (m_commandLine != nullptr) {
            m_commandLine->logSuccess(
              std::string("Display mode: ") +
              displayModeLabel(m_window->displayMode()));
          }
        }
        continue;
      }
      if (event.key == KeyCode::F3) {
        if (m_environment != nullptr) {
          const bool current = m_environment->getVar("showFPS").valueAsBool;
          m_environment->setVar("showFPS", !current);
          if (m_commandLine != nullptr) {
            m_commandLine->logSuccess(std::string("FPS overlay: ") +
                                      (!current ? "on" : "off"));
          }
        }
        continue;
      }
      if (event.key == KeyCode::F5) {
        if (m_assetManager != nullptr) {
          const size_t queued = m_assetManager->reloadAll();
          if (m_commandLine != nullptr) {
            m_commandLine->logNormal("Asset reloads queued: " +
                                     std::to_string(queued));
          }
        }
        continue;
      }
    }

    remainingKeys.push(event);
  }

  keyQueue.swap(remainingKeys);
}

void
Illumo::beginUpdate(double dt)
{
  if (!m_initialized) {
    return;
  }
  ILLUMO_PROFILE_ZONE("Illumo.BeginUpdate");
  m_frameProfiler.mark(FramePhase::Input);
  m_inputManager->update();
  processGlobalHotkeys();
  m_frameProfiler.mark(FramePhase::Camera);
  m_camera->Update(static_cast<float>(dt));
}

void
Illumo::endUpdate()
{
  if (!m_initialized) {
    return;
  }
  m_frameProfiler.mark(FramePhase::Other);
  // Key/char queues are per-frame events. Unconsumed leftovers must not
  // retrigger on the next update.
  m_inputManager->clearKeyQueue();
  m_inputManager->clearCharQueue();
}

static PooledRenderTargetDesc
makeBloomMipTargetDesc(int mipIndex)
{
  static constexpr const char* kBloomMipNames[5] = {
    "BloomMip0", "BloomMip1", "BloomMip2", "BloomMip3", "BloomMip4"
  };
  PooledRenderTargetDesc desc;
  desc.name = kBloomMipNames[std::clamp(mipIndex, 0, 4)];
  desc.windowRelative = true;
  desc.mipLevel = std::clamp(mipIndex, 0, 4) + 1;
  FramebufferAttachmentDesc color;
  color.format = TextureFormat::RGBA16F;
  color.filter = TextureFilter::Linear;
  color.wrap = TextureWrap::ClampToEdge;
  desc.colorAttachments.push_back(color);
  return desc;
}

void
Illumo::configureScenePipeline()
{
  if (!m_scene || !m_renderer || !m_environment) {
    return;
  }

  const RenderPipelineConfig pipeline =
    RenderPipelineConfig::fromEnv(m_environment.get());

  bool has3DWorld = false;
  const std::vector<DrawableBase*>& worldDrawables =
    m_scene->drawablesIn(RenderLayerId::World);
  for (size_t i = 0; i < worldDrawables.size(); ++i) {
    const DrawableBase* d = worldDrawables[i];
    if (d != nullptr && d->isVisible() && d->has3DWorldContent()) {
      has3DWorld = true;
      break;
    }
  }

  ConfiguredPipelineMode mode = ConfiguredPipelineMode::None;
  if (pipeline.hdrEnabled && (has3DWorld || pipeline.canvasBloomEnabled)) {
    mode = ConfiguredPipelineMode::HdrForward;
  } else if (pipeline.motionBlurEnabled) {
    mode = ConfiguredPipelineMode::LdrMotionBlur;
  }

  if (m_pipelineConfigured && m_configuredPipelineMode == mode &&
      m_configuredPipeline == pipeline) {
    return;
  }

  if (mode == ConfiguredPipelineMode::HdrForward) {
    m_renderer->ensureBuiltinStyles();
    const RenderStyleHandle compositeStyle =
      m_renderer->getBuiltinStyleHandle(RenderStyleId::HdrComposite);
    const RenderStyleHandle downStyle =
      m_renderer->getBuiltinStyleHandle(RenderStyleId::BloomDownsample);
    const RenderStyleHandle upStyle =
      m_renderer->getBuiltinStyleHandle(RenderStyleId::BloomUpsample);
    const RenderStyleHandle fxaaStyle =
      m_renderer->getBuiltinStyleHandle(RenderStyleId::FxaaResolve);

    const bool stylesOk =
      compositeStyle.isValid() &&
      (!pipeline.bloomEnabled || (downStyle.isValid() && upStyle.isValid())) &&
      (!pipeline.fxaaEnabled || fxaaStyle.isValid());

    PooledRenderTargetDesc hdrGeomDesc;
    hdrGeomDesc.name = "WorldHdrVelocity";
    hdrGeomDesc.windowRelative = true;
    FramebufferAttachmentDesc hdrColor;
    hdrColor.format = TextureFormat::RGBA16F;
    hdrColor.filter = TextureFilter::Linear;
    hdrColor.wrap = TextureWrap::ClampToEdge;
    hdrGeomDesc.colorAttachments.push_back(hdrColor);
    FramebufferAttachmentDesc hdrVelocity;
    hdrVelocity.format = TextureFormat::RG16F;
    hdrVelocity.filter = TextureFilter::Nearest;
    hdrVelocity.wrap = TextureWrap::ClampToEdge;
    hdrGeomDesc.colorAttachments.push_back(hdrVelocity);
    hdrGeomDesc.depthStencilFormat = TextureFormat::Depth24;

    const std::array<int, 2> winDims =
      m_window
        ? m_window->getWindowDimensions()
        : std::array<int, 2>{ kDefaultWindowWidth, kDefaultWindowHeight };
    const PooledRenderTarget probe = m_renderer->getRenderTargetPool().acquire(
      hdrGeomDesc, std::max(1, winDims[0]), std::max(1, winDims[1]));

    if (!stylesOk || !probe.isValid()) {
      if (!m_hdrFallbackWarned) {
        m_hdrFallbackWarned = true;
        Logger::LogWarning("Illumo: HDR render targets or styles unavailable; "
                           "falling back to LDR backbuffer");
      }
      m_pipelineConfigured = true;
      m_configuredPipelineMode = mode;
      m_configuredPipeline = pipeline;
      m_scene->SetDefaultLayerPasses(RenderLayerId::World, {});
      return;
    }

    std::vector<RenderPassDesc> worldPasses;

    RenderPassDesc geomPass;
    geomPass.name = "WorldHdrGeomPass";
    geomPass.type = PassType::Draw;
    geomPass.useScreenTarget = false;
    geomPass.pooledTargetName = hdrGeomDesc.name;
    geomPass.targetDesc = hdrGeomDesc;
    geomPass.clear.clearColor = true;
    geomPass.clear.clearColorValue = { 0.01f, 0.01f, 0.01f, 1.0f };
    geomPass.clear.clearDepth = true;
    worldPasses.push_back(std::move(geomPass));

    const bool runBloom =
      pipeline.bloomEnabled && pipeline.bloomIntensity > 0.0f;
    const int mipCount = std::clamp(pipeline.bloomMipCount, 1, 5);
    if (runBloom) {
      for (int m = 0; m < mipCount; ++m) {
        const PooledRenderTargetDesc mipDesc = makeBloomMipTargetDesc(m);
        RenderPassDesc downPass;
        downPass.name = std::string("BloomDown") + std::to_string(m);
        downPass.type = PassType::PostProcess;
        downPass.useScreenTarget = false;
        downPass.pooledTargetName = mipDesc.name;
        downPass.targetDesc = mipDesc;
        downPass.styleHandle = downStyle;

        PassInputTargetBinding srcBinding;
        srcBinding.targetName = (m == 0) ? std::string("WorldHdrVelocity")
                                         : makeBloomMipTargetDesc(m - 1).name;
        srcBinding.attachmentIndex = 0;
        srcBinding.slot = 0;
        srcBinding.samplerUniformName = "uSourceTexture";
        downPass.inputTargetTextures.push_back(srcBinding);
        downPass.uniformInts.push_back(
          PassUniformInt{ "uKarisAverage", (m == 0) ? 1 : 0 });
        worldPasses.push_back(std::move(downPass));
      }

      for (int m = mipCount - 2; m >= 0; --m) {
        const PooledRenderTargetDesc dstMipDesc = makeBloomMipTargetDesc(m);
        RenderPassDesc upPass;
        upPass.name = std::string("BloomUp") + std::to_string(m);
        upPass.type = PassType::PostProcess;
        upPass.useScreenTarget = false;
        upPass.pooledTargetName = dstMipDesc.name;
        upPass.targetDesc = dstMipDesc;
        upPass.styleHandle = upStyle;

        PassInputTargetBinding srcBinding;
        srcBinding.targetName = makeBloomMipTargetDesc(m + 1).name;
        srcBinding.attachmentIndex = 0;
        srcBinding.slot = 0;
        srcBinding.samplerUniformName = "uSourceTexture";
        upPass.inputTargetTextures.push_back(srcBinding);
        upPass.uniformFloats.push_back(
          PassUniformFloat{ "uFilterRadius", 1.0f });
        worldPasses.push_back(std::move(upPass));
      }
    }

    RenderPassDesc compositePass;
    compositePass.name = "HdrComposite";
    compositePass.type = PassType::PostProcess;
    compositePass.styleHandle = compositeStyle;
    if (pipeline.fxaaEnabled) {
      compositePass.useScreenTarget = false;
      compositePass.pooledTargetName = "WorldLdrPreFxaa";
      compositePass.targetDesc.name = "WorldLdrPreFxaa";
      compositePass.targetDesc.windowRelative = true;
      FramebufferAttachmentDesc ldrColor;
      ldrColor.format = TextureFormat::RGBA8;
      ldrColor.filter = TextureFilter::Linear;
      ldrColor.wrap = TextureWrap::ClampToEdge;
      compositePass.targetDesc.colorAttachments.push_back(ldrColor);
    } else {
      compositePass.useScreenTarget = true;
    }

    PassInputTargetBinding hdrBinding;
    hdrBinding.targetName = "WorldHdrVelocity";
    hdrBinding.attachmentIndex = 0;
    hdrBinding.slot = 0;
    hdrBinding.samplerUniformName = "uColorTexture";
    compositePass.inputTargetTextures.push_back(hdrBinding);

    PassInputTargetBinding velBinding;
    velBinding.targetName = "WorldHdrVelocity";
    velBinding.attachmentIndex = 1;
    velBinding.slot = 1;
    velBinding.samplerUniformName = "uVelocityTexture";
    compositePass.inputTargetTextures.push_back(velBinding);

    if (runBloom) {
      PassInputTargetBinding bloomBinding;
      bloomBinding.targetName = "BloomMip0";
      bloomBinding.attachmentIndex = 0;
      bloomBinding.slot = 2;
      bloomBinding.samplerUniformName = "uBloomTexture";
      compositePass.inputTargetTextures.push_back(bloomBinding);
    }

    compositePass.uniformInts.push_back(PassUniformInt{
      "uMotionBlurEnabled", pipeline.motionBlurEnabled ? 1 : 0 });
    compositePass.uniformFloats.push_back(
      PassUniformFloat{ "uMotionBlurAmount", pipeline.motionBlurAmount });
    compositePass.uniformFloats.push_back(
      PassUniformFloat{ "uMotionBlurMax", pipeline.motionBlurMax });
    compositePass.uniformInts.push_back(
      PassUniformInt{ "uMotionBlurSamples", pipeline.motionBlurSamples });
    compositePass.uniformInts.push_back(
      PassUniformInt{ "uBloomEnabled", runBloom ? 1 : 0 });
    compositePass.uniformFloats.push_back(
      PassUniformFloat{ "uBloomIntensity", pipeline.bloomIntensity });
    compositePass.uniformFloats.push_back(
      PassUniformFloat{ "uExposure", pipeline.exposure });
    compositePass.uniformInts.push_back(PassUniformInt{
      "uTonemapper",
      std::clamp(
        pipeline.tonemapper, 0, RenderPipelineConfig::kMaxTonemapper) });
    compositePass.uniformInts.push_back(
      PassUniformInt{ "uStoreLumaInAlpha", pipeline.fxaaEnabled ? 1 : 0 });
    worldPasses.push_back(std::move(compositePass));

    if (pipeline.fxaaEnabled) {
      RenderPassDesc fxaaPass;
      fxaaPass.name = "FxaaResolve";
      fxaaPass.type = PassType::PostProcess;
      fxaaPass.useScreenTarget = true;
      fxaaPass.styleHandle = fxaaStyle;

      PassInputTargetBinding ldrInput;
      ldrInput.targetName = "WorldLdrPreFxaa";
      ldrInput.attachmentIndex = 0;
      ldrInput.slot = 0;
      ldrInput.samplerUniformName = "uColorTexture";
      fxaaPass.inputTargetTextures.push_back(ldrInput);
      worldPasses.push_back(std::move(fxaaPass));
    }

    m_pipelineConfigured = true;
    m_configuredPipelineMode = mode;
    m_configuredPipeline = pipeline;
    m_scene->SetDefaultLayerPasses(RenderLayerId::World,
                                   std::move(worldPasses));
    return;
  }

  if (mode == ConfiguredPipelineMode::LdrMotionBlur) {
    RenderPassDesc geomPass;
    geomPass.name = "WorldGeomPass";
    geomPass.type = PassType::Draw;
    geomPass.useScreenTarget = false;
    geomPass.pooledTargetName = "WorldColorVelocity";
    geomPass.targetDesc.name = "WorldColorVelocity";
    geomPass.targetDesc.windowRelative = true;

    FramebufferAttachmentDesc color0;
    color0.format = TextureFormat::RGBA8;
    geomPass.targetDesc.colorAttachments.push_back(color0);

    FramebufferAttachmentDesc color1;
    color1.format = TextureFormat::RG16F;
    geomPass.targetDesc.colorAttachments.push_back(color1);

    geomPass.targetDesc.depthStencilFormat = TextureFormat::Depth24;
    geomPass.clear.clearColor = true;
    geomPass.clear.clearColorValue = { 0.1f, 0.1f, 0.1f, 1.0f };
    geomPass.clear.clearDepth = true;

    RenderPassDesc postPass;
    postPass.name = "MotionBlurResolve";
    postPass.type = PassType::PostProcess;
    postPass.useScreenTarget = true;
    postPass.styleHandle =
      m_renderer->getBuiltinStyleHandle(RenderStyleId::MotionBlur);

    PassInputTargetBinding colorBinding;
    colorBinding.targetName = "WorldColorVelocity";
    colorBinding.attachmentIndex = 0;
    colorBinding.slot = 0;
    colorBinding.samplerUniformName = "uColorTexture";
    postPass.inputTargetTextures.push_back(colorBinding);

    PassInputTargetBinding velBinding;
    velBinding.targetName = "WorldColorVelocity";
    velBinding.attachmentIndex = 1;
    velBinding.slot = 1;
    velBinding.samplerUniformName = "uVelocityTexture";
    postPass.inputTargetTextures.push_back(velBinding);

    postPass.uniformFloats.push_back(
      PassUniformFloat{ "uMotionBlurAmount", pipeline.motionBlurAmount });
    postPass.uniformFloats.push_back(
      PassUniformFloat{ "uMotionBlurMax", pipeline.motionBlurMax });
    postPass.uniformInts.push_back(
      PassUniformInt{ "uMotionBlurSamples", pipeline.motionBlurSamples });

    std::vector<RenderPassDesc> worldPasses;
    worldPasses.push_back(std::move(geomPass));
    worldPasses.push_back(std::move(postPass));
    m_pipelineConfigured = true;
    m_configuredPipelineMode = mode;
    m_configuredPipeline = pipeline;
    m_scene->SetDefaultLayerPasses(RenderLayerId::World,
                                   std::move(worldPasses));
    return;
  }

  m_pipelineConfigured = true;
  m_configuredPipelineMode = mode;
  m_configuredPipeline = pipeline;
  m_scene->SetDefaultLayerPasses(RenderLayerId::World, {});
}

DrawList*
Illumo::beginRender()
{
  if (!m_initialized) {
    return nullptr;
  }
  ILLUMO_PROFILE_ZONE("Illumo.BeginRender");
  m_frameProfiler.mark(FramePhase::ScenePreparation);
  m_scene->ClearDrawables();
  return m_scene.get();
}

void
Illumo::endRender()
{
  if (!m_initialized) {
    return;
  }
  ILLUMO_PROFILE_ZONE("Illumo.Render");
  m_frameProfiler.mark(FramePhase::Assets);
  {
    ILLUMO_PROFILE_ZONE("Illumo.AssetPump");
    m_assetManager->pump();
  }
  configureScenePipeline();
  m_frameProfiler.mark(FramePhase::Commands);
  {
    ILLUMO_PROFILE_ZONE("Illumo.BeginFrame");
    m_renderer->BeginFrame();
  }
  {
    ILLUMO_PROFILE_ZONE("Illumo.RenderScene");
    m_renderer->RenderScene(m_scene.get(), m_camera.get());
  }
  {
    ILLUMO_PROFILE_ZONE("Illumo.EndFrame");
    if (m_frameProfiler.recording()) {
      FrameProfiler::TimePoint presentationStart;
      m_renderer->EndFrame(&presentationStart);
      m_frameProfiler.mark(FramePhase::Presentation, presentationStart);
    } else {
      m_renderer->EndFrame();
    }
  }
  m_frameProfiler.mark(FramePhase::Other);
}

void
Illumo::shutdown() noexcept
{
  const bool wasRunning = m_initialized;
  releaseServices();
  if (wasRunning) {
    Logger::LogTrace("Engine services released");
  }
}

void
Illumo::releaseServices()
{
  ILLUMO_PROFILE_ZONE("Illumo.ReleaseServices");
  GLString::setRenderWindow(nullptr);
  m_scene.reset();
  m_inputManager.reset();
  Logger::setContext(m_environment.get(), nullptr);
  m_commandLine.reset();
  m_commandRegistry.reset();
  m_assetManager.reset();
  m_renderer.reset();
  m_camera.reset();
  m_window.reset();
  clearContext();
  m_initialized = false;
  m_pipelineConfigured = false;
  m_hdrFallbackWarned = false;
  m_configuredPipelineMode = ConfiguredPipelineMode::None;
}

void
Illumo::clearContext()
{
  m_context = IllumoContext{};
}

IllumoContext&
Illumo::context()
{
  return m_context;
}

const IllumoContext&
Illumo::context() const
{
  return m_context;
}

bool
Illumo::shouldClose() const
{
  return !m_window || m_window->shouldWindowClose();
}

void
Illumo::deferClose()
{
  if (m_window) {
    m_window->cancelCloseRequest();
    m_window->clearRestartRequest();
  }
}
