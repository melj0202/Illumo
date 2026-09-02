#include "MeshViewerModule.h"

#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Platform/SaveLoad.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/MeshLoader.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/IEnvVars.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <utility>

static bool
viewerContextComplete(const IllumoContext* context)
{
  return context != nullptr && context->envVars != nullptr &&
         context->window != nullptr && context->camera != nullptr &&
         context->renderer != nullptr && context->inputManager != nullptr &&
         context->commandLine != nullptr && context->scene != nullptr;
}

MeshViewerModule::MeshViewerModule(std::string initialMeshPath)
  : m_initialMeshPath(std::move(initialMeshPath))
  , m_showGrid(true)
  , m_showWireframe(false)
  , m_showAxes(true)
  , m_isOrbiting(false)
  , m_isPanning(false)
  , m_mouseWasDown(false)
  , m_lastMouseX(0.0)
  , m_lastMouseY(0.0)
{
}

MeshViewerModule::~MeshViewerModule() = default;

bool
MeshViewerModule::Start(IllumoContext* context)
{
  if (!viewerContextComplete(context)) {
    Logger::LogError("MeshViewerModule::Start: IllumoContext missing services");
    ic = context;
    return false;
  }
  ic = context;

  if (ic->envVars != nullptr) {
    const std::string gridVar = ic->envVars->getVar("showGrid").value;
    if (!gridVar.empty()) {
      m_showGrid = (gridVar == "1" || gridVar == "true");
    }
    const std::string axesVar = ic->envVars->getVar("showAxes").value;
    if (!axesVar.empty()) {
      m_showAxes = (axesVar == "1" || axesVar == "true");
    }
    const std::string wireVar = ic->envVars->getVar("showWireframe").value;
    if (!wireVar.empty()) {
      m_showWireframe = (wireVar == "1" || wireVar == "true");
    }
  }

  m_gridVisual = std::make_unique<MeshVisual>();
  m_gridVisual->prepare(ic->renderer);

  m_meshVisual = std::make_unique<MeshVisual>();
  m_meshVisual->prepare(ic->renderer);

  m_wireframeVisual = std::make_unique<MeshVisual>();
  m_wireframeVisual->prepare(ic->renderer);

  m_ui = std::make_unique<MeshViewerUi>(ic->window, ic->renderer);
  applyLightingFromEnv();
  applyShadowsFromEnv();
  applyMotionBlurFromEnv();

  if (ic->envVars != nullptr) {
    const std::string fontSizeVar = ic->envVars->getVar("fontSize").value;
    if (!fontSizeVar.empty()) {
      try {
        m_ui->setFontSize(std::stof(fontSizeVar));
      } catch (...) {
      }
    }
  }

  rebuildGrid();

  if (m_initialMeshPath.empty() && ic->envVars != nullptr) {
    m_initialMeshPath = ic->envVars->getVar("LaunchMesh").value;
  }
  if (!m_initialMeshPath.empty()) {
    loadMesh(m_initialMeshPath);
  } else {
    m_camera.reset();
    m_camera.applyTo(ic->camera);
    syncUiMetadata();
  }

  return true;
}

void
MeshViewerModule::Exit()
{
  m_ui.reset();
  m_wireframeVisual.reset();
  m_meshVisual.reset();
  m_gridVisual.reset();
  m_meshData.clear();
  m_meshPath.clear();
}

bool
MeshViewerModule::loadMesh(const std::string& path)
{
  if (path.empty()) {
    return false;
  }

  MeshLoadOptions options;
  options.triangulate = true;
  options.generateNormalsIfMissing = true;
  options.flipTexCoordsV = true;
  options.centerAndNormalize = true;
  options.targetRadius = 1.0f;

  const MeshLoadResult result = MeshLoader::loadFromFile(path, options);
  if (!result.success) {
    if (ic != nullptr && ic->commandLine != nullptr) {
      ic->commandLine->logError("Failed to load mesh: " + result.error);
    }
    if (m_ui) {
      m_ui->showToast("Error: " + result.error,
                      ColorRgba{ 245, 100, 110, 255 });
    }
    return false;
  }

  m_meshPath = path;
  m_meshData = result.mesh;

  rebuildMeshVisual();
  rebuildWireframe();

  m_camera.frameBounds(m_meshData.minBounds, m_meshData.maxBounds);
  if (ic != nullptr && ic->camera != nullptr) {
    m_camera.applyTo(ic->camera);
  }

  syncUiMetadata();

  const std::string filename = std::filesystem::path(path).filename().string();
  if (m_ui) {
    m_ui->showToast("Loaded: " + filename, ColorRgba{ 60, 220, 120, 255 });
  }

  return true;
}

bool
MeshViewerModule::loadMeshFromMemory(const std::string& content,
                                     const std::string& name)
{
  MeshLoadOptions options;
  options.triangulate = true;
  options.generateNormalsIfMissing = true;
  options.flipTexCoordsV = true;
  options.centerAndNormalize = true;
  options.targetRadius = 1.0f;

  const MeshLoadResult result =
    MeshLoader::loadFromMemory(content, options, "");
  if (!result.success) {
    return false;
  }

  m_meshPath = name;
  m_meshData = result.mesh;

  rebuildMeshVisual();
  rebuildWireframe();

  m_camera.frameBounds(m_meshData.minBounds, m_meshData.maxBounds);
  if (ic != nullptr && ic->camera != nullptr) {
    m_camera.applyTo(ic->camera);
  }

  syncUiMetadata();
  return true;
}

void
MeshViewerModule::setShowGrid(bool show)
{
  m_showGrid = show;
  rebuildGrid();
  if (m_ui) {
    m_ui->setDisplayOptions(m_showGrid, m_showWireframe, m_showAxes);
  }
}

void
MeshViewerModule::setShowWireframe(bool show)
{
  m_showWireframe = show;
  rebuildWireframe();
  if (m_ui) {
    m_ui->setDisplayOptions(m_showGrid, m_showWireframe, m_showAxes);
  }
}

void
MeshViewerModule::setShowAxes(bool show)
{
  m_showAxes = show;
  rebuildGrid();
  if (m_ui) {
    m_ui->setDisplayOptions(m_showGrid, m_showWireframe, m_showAxes);
  }
}

void
MeshViewerModule::resetCamera()
{
  if (!m_meshData.isEmpty()) {
    m_camera.frameBounds(m_meshData.minBounds, m_meshData.maxBounds);
  } else {
    m_camera.reset();
  }
  if (ic != nullptr && ic->camera != nullptr) {
    m_camera.applyTo(ic->camera);
  }
  if (m_ui) {
    m_ui->showToast("Camera reset", ColorRgba{ 66, 214, 210, 255 });
  }
}

SaveLoadDialogSpec
MeshViewerModule::dialogSpec() const
{
  SaveLoadDialogSpec spec;
  spec.fileDescription = "3D Wavefront Mesh (*.obj)";
  spec.defaultFilename = "model.obj";
  spec.extensionPattern = "*.obj;*.OBJ";
  return spec;
}

bool
MeshViewerModule::openMeshDialog()
{
  const std::string path = SaveLoad::GetLoadLocation(dialogSpec());
  if (path.empty()) {
    return false;
  }
  return loadMesh(path);
}

void
MeshViewerModule::rebuildGrid()
{
  if (!m_gridVisual || ic == nullptr || ic->renderer == nullptr) {
    return;
  }
  m_gridVisual->clearPrimitives();

  if (m_showGrid) {
    const int halfCount = 15;
    const float spacing = 0.5f;
    const float extent = static_cast<float>(halfCount) * spacing;
    const ColorRgba minorColor{ 45, 55, 70, 200 };
    const ColorRgba majorColor{ 70, 85, 110, 255 };

    for (int i = -halfCount; i <= halfCount; ++i) {
      if (i == 0) {
        continue;
      }
      const float pos = static_cast<float>(i) * spacing;
      const ColorRgba color = (i % 5 == 0) ? majorColor : minorColor;
      m_gridVisual->addLine(
        glm::vec3(-extent, 0.0f, pos), glm::vec3(extent, 0.0f, pos), color);
      m_gridVisual->addLine(
        glm::vec3(pos, 0.0f, -extent), glm::vec3(pos, 0.0f, extent), color);
    }
  }

  if (m_showAxes) {
    const float axisLength = 2.0f;
    // X Axis - Red
    m_gridVisual->addLine(glm::vec3(0.0f, 0.0f, 0.0f),
                          glm::vec3(axisLength, 0.0f, 0.0f),
                          ColorRgba{ 230, 65, 65, 255 });
    // Y Axis - Green
    m_gridVisual->addLine(glm::vec3(0.0f, 0.0f, 0.0f),
                          glm::vec3(0.0f, axisLength, 0.0f),
                          ColorRgba{ 65, 215, 95, 255 });
    // Z Axis - Blue
    m_gridVisual->addLine(glm::vec3(0.0f, 0.0f, 0.0f),
                          glm::vec3(0.0f, 0.0f, axisLength),
                          ColorRgba{ 65, 125, 235, 255 });
  }
}

void
MeshViewerModule::rebuildWireframe()
{
  if (!m_wireframeVisual || ic == nullptr || ic->renderer == nullptr) {
    return;
  }
  m_wireframeVisual->clearPrimitives();

  if (!m_showWireframe || m_meshData.isEmpty()) {
    return;
  }

  // Draw bounding box wireframe
  const glm::vec3 center = (m_meshData.minBounds + m_meshData.maxBounds) * 0.5f;
  const glm::vec3 halfExtent =
    (m_meshData.maxBounds - m_meshData.minBounds) * 0.5f;
  m_wireframeVisual->addWireCube(
    center, halfExtent * 1.01f, ColorRgba{ 255, 200, 50, 200 });

  // Draw wireframe triangles
  const ColorRgba wireColor{ 200, 220, 255, 140 };
  const size_t triCount = m_meshData.indices.size() / 3;
  for (size_t t = 0; t < triCount; ++t) {
    const uint32_t i0 = m_meshData.indices[t * 3 + 0];
    const uint32_t i1 = m_meshData.indices[t * 3 + 1];
    const uint32_t i2 = m_meshData.indices[t * 3 + 2];
    if (i0 < m_meshData.vertices.size() && i1 < m_meshData.vertices.size() &&
        i2 < m_meshData.vertices.size()) {
      const glm::vec3& p0 = m_meshData.vertices[i0].position;
      const glm::vec3& p1 = m_meshData.vertices[i1].position;
      const glm::vec3& p2 = m_meshData.vertices[i2].position;
      m_wireframeVisual->addLine(p0, p1, wireColor);
      m_wireframeVisual->addLine(p1, p2, wireColor);
      m_wireframeVisual->addLine(p2, p0, wireColor);
    }
  }
}

void
MeshViewerModule::applyLightingFromEnv()
{
  if (m_meshVisual == nullptr || ic == nullptr || ic->envVars == nullptr) {
    return;
  }

  const EnvVar& lightingVar = ic->envVars->getVar("lightingEnabled");
  if (!lightingVar.value.empty()) {
    m_meshVisual->setLightingEnabled(lightingVar.valueAsBool);
  }

  glm::vec3 lightDirection = m_meshVisual->getLightDirection();
  const EnvVar& lightDirX = ic->envVars->getVar("lightDirX");
  const EnvVar& lightDirY = ic->envVars->getVar("lightDirY");
  const EnvVar& lightDirZ = ic->envVars->getVar("lightDirZ");
  if (!lightDirX.value.empty()) {
    lightDirection.x = static_cast<float>(lightDirX.valueAsDouble);
  }
  if (!lightDirY.value.empty()) {
    lightDirection.y = static_cast<float>(lightDirY.valueAsDouble);
  }
  if (!lightDirZ.value.empty()) {
    lightDirection.z = static_cast<float>(lightDirZ.valueAsDouble);
  }
  m_meshVisual->setLightDirection(lightDirection);

  glm::vec3 lightColor = m_meshVisual->getLightColor();
  const EnvVar& lightColorR = ic->envVars->getVar("lightColorR");
  const EnvVar& lightColorG = ic->envVars->getVar("lightColorG");
  const EnvVar& lightColorB = ic->envVars->getVar("lightColorB");
  if (!lightColorR.value.empty()) {
    lightColor.x = static_cast<float>(lightColorR.valueAsDouble);
  }
  if (!lightColorG.value.empty()) {
    lightColor.y = static_cast<float>(lightColorG.valueAsDouble);
  }
  if (!lightColorB.value.empty()) {
    lightColor.z = static_cast<float>(lightColorB.valueAsDouble);
  }
  m_meshVisual->setLightColor(lightColor);

  glm::vec3 ambientColor = m_meshVisual->getAmbientColor();
  const EnvVar& ambientColorR = ic->envVars->getVar("ambientColorR");
  const EnvVar& ambientColorG = ic->envVars->getVar("ambientColorG");
  const EnvVar& ambientColorB = ic->envVars->getVar("ambientColorB");
  if (!ambientColorR.value.empty()) {
    ambientColor.x = static_cast<float>(ambientColorR.valueAsDouble);
  }
  if (!ambientColorG.value.empty()) {
    ambientColor.y = static_cast<float>(ambientColorG.valueAsDouble);
  }
  if (!ambientColorB.value.empty()) {
    ambientColor.z = static_cast<float>(ambientColorB.valueAsDouble);
  }
  m_meshVisual->setAmbientColor(ambientColor);
}

void
MeshViewerModule::applyShadowsFromEnv()
{
  if (m_meshVisual == nullptr || ic == nullptr || ic->envVars == nullptr) {
    return;
  }

  const EnvVar& shadowsVar = ic->envVars->getVar("shadowsEnabled");
  if (!shadowsVar.value.empty()) {
    m_meshVisual->setShadowsEnabled(shadowsVar.valueAsBool);
  }

  const EnvVar& shadowMapSizeVar = ic->envVars->getVar("shadowMapSize");
  if (!shadowMapSizeVar.value.empty()) {
    m_meshVisual->setShadowMapSize(
      static_cast<int>(shadowMapSizeVar.valueAsLong));
  }

  const EnvVar& shadowRadiusVar = ic->envVars->getVar("shadowRadius");
  if (!shadowRadiusVar.value.empty()) {
    m_meshVisual->setShadowRadius(
      static_cast<float>(shadowRadiusVar.valueAsDouble));
  }

  const EnvVar& lightDistanceVar = ic->envVars->getVar("lightDistance");
  if (!lightDistanceVar.value.empty()) {
    m_meshVisual->setLightDistance(
      static_cast<float>(lightDistanceVar.valueAsDouble));
  }

  const EnvVar& shadowBiasVar = ic->envVars->getVar("shadowBias");
  if (!shadowBiasVar.value.empty()) {
    m_meshVisual->setShadowBias(
      static_cast<float>(shadowBiasVar.valueAsDouble));
  }

  const EnvVar& shadowSlopeVar = ic->envVars->getVar("shadowSlopeScale");
  if (!shadowSlopeVar.value.empty()) {
    m_meshVisual->setShadowSlopeScale(
      static_cast<float>(shadowSlopeVar.valueAsDouble));
  }

  const EnvVar& shadowOffsetVar = ic->envVars->getVar("shadowNormalOffset");
  if (!shadowOffsetVar.value.empty()) {
    m_meshVisual->setShadowNormalOffset(
      static_cast<float>(shadowOffsetVar.valueAsDouble));
  }

  const EnvVar& shadowPcfVar = ic->envVars->getVar("shadowPcf");
  if (!shadowPcfVar.value.empty()) {
    m_meshVisual->setShadowPcfEnabled(shadowPcfVar.valueAsBool);
  }
}

void
MeshViewerModule::applyMotionBlurFromEnv()
{
  if (m_meshVisual == nullptr || ic == nullptr || ic->envVars == nullptr) {
    return;
  }

  const EnvVar& enabledVar = ic->envVars->getVar("motionBlurEnabled");
  if (!enabledVar.value.empty()) {
    m_meshVisual->setMotionBlurEnabled(enabledVar.valueAsBool);
  }

  const EnvVar& amountVar = ic->envVars->getVar("motionBlurAmount");
  if (!amountVar.value.empty()) {
    m_meshVisual->setMotionBlurAmount(
      static_cast<float>(amountVar.valueAsDouble));
  }

  const EnvVar& maxVar = ic->envVars->getVar("motionBlurMax");
  if (!maxVar.value.empty()) {
    m_meshVisual->setMotionBlurMax(static_cast<float>(maxVar.valueAsDouble));
  }
}

void
MeshViewerModule::rebuildMeshVisual()
{
  if (!m_meshVisual || ic == nullptr || ic->renderer == nullptr) {
    return;
  }
  m_meshVisual->clearPrimitives();

  if (m_meshData.isEmpty()) {
    return;
  }

  m_meshVisual->addMesh(m_meshData, ColorRgba{ 225, 230, 240, 255 });
}

void
MeshViewerModule::syncUiMetadata()
{
  if (!m_ui) {
    return;
  }
  MeshMetadata meta;
  if (!m_meshData.isEmpty()) {
    meta.hasMesh = true;
    meta.filename = m_meshPath.empty()
                      ? "memory.obj"
                      : std::filesystem::path(m_meshPath).filename().string();
    meta.vertexCount = m_meshData.vertices.size();
    meta.triangleCount = m_meshData.indices.size() / 3;
    meta.submeshCount =
      m_meshData.submeshes.empty() ? 1 : m_meshData.submeshes.size();
    meta.materialCount = m_meshData.materials.size();
    meta.dimensions = m_meshData.maxBounds - m_meshData.minBounds;
  } else {
    meta.hasMesh = false;
  }
  m_ui->setMeshMetadata(meta);
  m_ui->setDisplayOptions(m_showGrid, m_showWireframe, m_showAxes);
}

void
MeshViewerModule::handleAction(MeshViewerAction action)
{
  switch (action) {
    case MeshViewerAction::OpenMesh:
      openMeshDialog();
      break;
    case MeshViewerAction::ResetView:
      resetCamera();
      break;
    case MeshViewerAction::ToggleGrid:
      setShowGrid(!m_showGrid);
      break;
    case MeshViewerAction::ToggleWireframe:
      setShowWireframe(!m_showWireframe);
      break;
    case MeshViewerAction::ToggleAxes:
      setShowAxes(!m_showAxes);
      break;
    case MeshViewerAction::None:
    default:
      break;
  }
}

void
MeshViewerModule::updateCameraInput(double dt)
{
  if (ic == nullptr || ic->camera == nullptr || ic->inputManager == nullptr ||
      ic->window == nullptr) {
    return;
  }

  const std::array<double, 2> mouse = ic->window->getMouseCoords();
  const double deltaMouseX = mouse[0] - m_lastMouseX;
  const double deltaMouseY = mouse[1] - m_lastMouseY;

  const bool leftMouse =
    ic->inputManager->isMouseButtonPressed(KeyCode::MouseLeft);
  const bool rightMouse =
    ic->inputManager->isMouseButtonPressed(KeyCode::MouseRight);
  const bool middleMouse =
    ic->inputManager->isMouseButtonPressed(KeyCode::MouseMiddle);
  const bool shift = ic->inputManager->isShiftPressed();
  const bool alt = ic->inputManager->isAltPressed();
  const bool ctrl = ic->inputManager->isControlPressed();

  // Keyboard Navigation: WASD / Arrow keys for panning
  if (!ctrl) {
    const float panSpeed = 3.0f * static_cast<float>(dt);
    float panRight = 0.0f;
    float panUp = 0.0f;
    if (ic->inputManager->isKeyPressed(KeyCode::A) ||
        ic->inputManager->isKeyPressed(KeyCode::Left)) {
      panRight += panSpeed;
    }
    if (ic->inputManager->isKeyPressed(KeyCode::D) ||
        ic->inputManager->isKeyPressed(KeyCode::Right)) {
      panRight -= panSpeed;
    }
    if (ic->inputManager->isKeyPressed(KeyCode::W) ||
        ic->inputManager->isKeyPressed(KeyCode::Up)) {
      panUp += panSpeed;
    }
    if (ic->inputManager->isKeyPressed(KeyCode::S) ||
        ic->inputManager->isKeyPressed(KeyCode::Down)) {
      panUp -= panSpeed;
    }
    if (panRight != 0.0f || panUp != 0.0f) {
      m_camera.pan(panRight, panUp);
    }

    // Q / E for roll / tilt rotation
    const float rollSpeed = 2.0f * static_cast<float>(dt);
    if (ic->inputManager->isKeyPressed(KeyCode::Q)) {
      m_camera.rotate(-rollSpeed);
    }
    if (ic->inputManager->isKeyPressed(KeyCode::E)) {
      m_camera.rotate(rollSpeed);
    }
  }

  // Mouse Pan: Middle Mouse OR (Shift + Left/Right Mouse)
  if (middleMouse || (shift && (leftMouse || rightMouse))) {
    m_isPanning = true;
    m_isOrbiting = false;
    const float factor = 0.0025f * m_camera.distance();
    m_camera.pan(static_cast<float>(deltaMouseX) * factor,
                 static_cast<float>(deltaMouseY) * factor);
  } else {
    m_isPanning = false;
  }

  // Mouse Orbit / Rotate: Left or Right Mouse without shift/alt
  if (!m_isPanning) {
    if ((leftMouse || rightMouse) && !alt) {
      m_isOrbiting = true;
      const float orbitSpeed = 0.008f;
      m_camera.orbit(static_cast<float>(deltaMouseX) * orbitSpeed,
                     static_cast<float>(deltaMouseY) * orbitSpeed);
    } else if ((leftMouse || rightMouse) && alt) {
      // Alt + drag = roll / rotate
      const float rollSpeed = 0.008f;
      m_camera.rotate(static_cast<float>(deltaMouseX) * rollSpeed);
    } else {
      m_isOrbiting = false;
    }
  }

  // Mouse Wheel Zoom
  double* scroll = ic->inputManager->getMouseScrollOffset();
  if (scroll != nullptr && *scroll != 0.0) {
    const float factor = *scroll > 0.0 ? 0.88f : 1.14f;
    m_camera.zoom(factor);
    *scroll = 0.0;
  }
}

void
MeshViewerModule::Update(double dt)
{
  if (ic == nullptr) {
    return;
  }

  const bool consoleOpen =
    ic->commandLine != nullptr && ic->commandLine->isOpen;

  MeshViewerAction action = MeshViewerAction::None;
  applyLightingFromEnv();
  applyShadowsFromEnv();
  applyMotionBlurFromEnv();

  if (m_ui) {
    action = m_ui->update(ic->inputManager, static_cast<float>(dt));
  }

  handleAction(action);

  if (!consoleOpen && ic->inputManager != nullptr) {
    // Hotkeys
    if (ic->inputManager->isKeyPressed(KeyCode::O)) {
      static bool oWasPressed = false;
      if (!oWasPressed) {
        openMeshDialog();
      }
      oWasPressed = true;
    } else {
      static bool oWasPressed = false;
      oWasPressed = false;
    }

    if (ic->inputManager->isKeyPressed(KeyCode::F) ||
        ic->inputManager->isKeyPressed(KeyCode::R)) {
      static bool rWasPressed = false;
      if (!rWasPressed) {
        resetCamera();
      }
      rWasPressed = true;
    } else {
      static bool rWasPressed = false;
      rWasPressed = false;
    }

    if (ic->inputManager->isKeyPressed(KeyCode::G)) {
      static bool gWasPressed = false;
      if (!gWasPressed) {
        setShowGrid(!m_showGrid);
      }
      gWasPressed = true;
    } else {
      static bool gWasPressed = false;
      gWasPressed = false;
    }

    if (ic->inputManager->isKeyPressed(KeyCode::X)) {
      static bool xWasPressed = false;
      if (!xWasPressed) {
        setShowWireframe(!m_showWireframe);
      }
      xWasPressed = true;
    } else {
      static bool xWasPressed = false;
      xWasPressed = false;
    }

    const bool uiConsumed = m_ui && m_ui->consumedPress();
    if (!uiConsumed) {
      updateCameraInput(dt);
    }
  }

  m_camera.update(static_cast<float>(dt));
  if (ic->camera != nullptr) {
    m_camera.applyTo(ic->camera);
  }

  if (m_ui) {
    m_ui->setCameraInfo(glm::degrees(m_camera.yaw()),
                        glm::degrees(m_camera.pitch()),
                        m_camera.distance());
  }

  if (ic->window != nullptr) {
    const std::array<double, 2> mouse = ic->window->getMouseCoords();
    m_lastMouseX = mouse[0];
    m_lastMouseY = mouse[1];
  }

  if (m_gridVisual) {
    m_gridVisual->prepare(ic->renderer);
  }
  if (m_meshVisual) {
    m_meshVisual->prepare(ic->renderer);
  }
  if (m_wireframeVisual) {
    m_wireframeVisual->prepare(ic->renderer);
  }
  if (m_ui) {
    m_ui->getVisual().prepare(ic->renderer);
  }
}

void
MeshViewerModule::DispatchDrawables(Scene* scene)
{
  if (scene == nullptr) {
    return;
  }
  if (m_showGrid && m_gridVisual) {
    scene->AddDrawable(m_gridVisual.get(), RenderLayerId::World);
  }
  if (m_meshVisual && !m_meshData.isEmpty()) {
    scene->AddDrawable(m_meshVisual.get(), RenderLayerId::World);
  }
  if (m_showWireframe && m_wireframeVisual) {
    scene->AddDrawable(m_wireframeVisual.get(), RenderLayerId::World);
  }
  if (m_ui) {
    scene->AddDrawable(m_ui.get(), RenderLayerId::UI);
  }
}
