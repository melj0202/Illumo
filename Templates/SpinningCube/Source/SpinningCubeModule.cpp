#include "SpinningCubeModule.h"

#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Primitives/MeshVisual.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Services/KeyCode.h>
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

SpinningCubeModule::SpinningCubeModule()
  : ic(nullptr)
  , m_cubeVisual(nullptr)
  , m_gridVisual(nullptr)
  , m_rotationAngle(0.0f)
  , m_rotationSpeed(1.2f)
  , m_paused(false)
  , m_showGrid(true)
{
}

SpinningCubeModule::~SpinningCubeModule() = default;

bool
SpinningCubeModule::Start(IllumoContext* context)
{
  ic = context;
  if (ic == nullptr) {
    return false;
  }

  if (ic->envVars != nullptr) {
    const EnvVar& speedVar = ic->envVars->getVar("cubeRotationSpeed");
    if (!speedVar.value.empty()) {
      m_rotationSpeed = static_cast<float>(speedVar.valueAsDouble);
    }
    const EnvVar& gridVar = ic->envVars->getVar("showGrid");
    if (!gridVar.value.empty()) {
      m_showGrid = (gridVar.value == "1" || gridVar.value == "true");
    }
  }

  // Setup 3D perspective camera looking at the cube center
  if (ic->camera != nullptr) {
    const glm::vec3 eye(0.0f, 1.8f, 3.8f);
    const glm::vec3 target(0.0f, 0.0f, 0.0f);
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    ic->camera->lookAt(eye, target, up);
    ic->camera->setPerspective(50.0f, 0.1f, 100.0f);
    ic->camera->setProjectionType(ProjectionType::Perspective);
  }

  // Reference floor grid
  m_gridVisual = std::make_unique<MeshVisual>();
  m_gridVisual->prepare(ic->renderer);
  m_gridVisual->setLightingEnabled(false);
  m_gridVisual->addGrid(12, 0.5f, ColorRgba{ 45, 55, 70, 180 });

  // Spinning 3D lit cube
  m_cubeVisual = std::make_unique<MeshVisual>();
  m_cubeVisual->prepare(ic->renderer);
  m_cubeVisual->setLightingEnabled(true);
  m_cubeVisual->setLightDirection(glm::normalize(glm::vec3(0.6f, 1.0f, 0.8f)));
  m_cubeVisual->setLightColor(glm::vec3(1.0f, 0.98f, 0.92f));
  m_cubeVisual->setAmbientColor(glm::vec3(0.25f, 0.28f, 0.35f));

  // Solid lit cube body
  m_cubeVisual->addSolidCube(
    glm::vec3(0.0f), glm::vec3(0.75f), ColorRgba{ 66, 160, 245, 255 });

  // Accent edge lines
  m_cubeVisual->addWireCube(
    glm::vec3(0.0f), glm::vec3(0.752f), ColorRgba{ 160, 220, 255, 255 });

  if (ic->commandLine != nullptr) {
    ic->commandLine->AppendStringLn(
      200,
      220,
      255,
      255,
      "Spinning Cube started. Space: Pause | R: Reset | G: Grid");
  }

  return true;
}

void
SpinningCubeModule::Update(double dt)
{
  if (ic == nullptr) {
    return;
  }

  // Input handling
  if (ic->inputManager != nullptr) {
    static bool spaceWasPressed = false;
    if (ic->inputManager->isKeyPressed(KeyCode::Space)) {
      if (!spaceWasPressed) {
        m_paused = !m_paused;
      }
      spaceWasPressed = true;
    } else {
      spaceWasPressed = false;
    }

    if (ic->inputManager->isKeyPressed(KeyCode::R)) {
      resetRotation();
    }

    static bool gWasPressed = false;
    if (ic->inputManager->isKeyPressed(KeyCode::G)) {
      if (!gWasPressed) {
        m_showGrid = !m_showGrid;
      }
      gWasPressed = true;
    } else {
      gWasPressed = false;
    }

    if (ic->inputManager->isKeyPressed(KeyCode::Up)) {
      m_rotationSpeed += static_cast<float>(dt) * 2.0f;
    }
    if (ic->inputManager->isKeyPressed(KeyCode::Down)) {
      m_rotationSpeed =
        std::max(0.0f, m_rotationSpeed - static_cast<float>(dt) * 2.0f);
    }
  }

  // Rotate cube
  if (!m_paused && dt > 0.0) {
    m_rotationAngle += static_cast<float>(dt) * m_rotationSpeed;
    if (m_rotationAngle > 6.2831853f * 100.0f) {
      m_rotationAngle = std::fmod(m_rotationAngle, 6.2831853f);
    }
  }

  if (m_cubeVisual != nullptr) {
    glm::mat4 model(1.0f);
    const glm::vec3 rotationAxis = glm::normalize(glm::vec3(0.5f, 1.0f, 0.25f));
    model = glm::rotate(model, m_rotationAngle, rotationAxis);
    m_cubeVisual->setModelMatrix(model);
    m_cubeVisual->prepare(ic->renderer);
  }

  if (m_gridVisual != nullptr) {
    m_gridVisual->prepare(ic->renderer);
  }
}

void
SpinningCubeModule::DispatchDrawables(Scene* scene)
{
  if (scene == nullptr) {
    return;
  }
  if (m_showGrid && m_gridVisual != nullptr) {
    scene->AddDrawable(m_gridVisual.get(), RenderLayerId::World);
  }
  if (m_cubeVisual != nullptr) {
    scene->AddDrawable(m_cubeVisual.get(), RenderLayerId::World);
  }
}

void
SpinningCubeModule::Exit()
{
  m_cubeVisual.reset();
  m_gridVisual.reset();
  ic = nullptr;
}
