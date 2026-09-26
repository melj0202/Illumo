#pragma once

#include <Illumo/Content/ProgramScene.h>
#include <Illumo/Rendering/Primitives/MeshVisual.h>
#include <array>
#include <memory>

// @PROJECT_NAME@'s one scene: a lit cube spinning over a reference grid. The
// program (Source/Wasm/SpinningCubeProgram.cpp) adds it to its SceneDirector;
// a program with more screens adds more scenes and switches between them.
// The native tests run it through a director too.
class SpinningCubeScene final : public ProgramScene
{
public:
  SpinningCubeScene();
  ~SpinningCubeScene() override;

  SpinningCubeScene(const SpinningCubeScene&) = delete;
  SpinningCubeScene& operator=(const SpinningCubeScene&) = delete;
  SpinningCubeScene(SpinningCubeScene&&) = delete;
  SpinningCubeScene& operator=(SpinningCubeScene&&) = delete;

  bool start(IllumoContext& context) override;
  // Registers the scene's console commands (cube_speed).
  void enter() override;
  void update(double dt) override;
  void dispatch(Scene& frame) override;
  void stop() override;

  float rotationAngle() const { return m_rotationAngle; }
  float rotationSpeed() const { return m_rotationSpeed; }
  void setRotationSpeed(float speed) { m_rotationSpeed = speed; }

  bool isPaused() const { return m_paused; }
  void setPaused(bool paused) { m_paused = paused; }

  bool showGrid() const { return m_showGrid; }
  void setShowGrid(bool show) { m_showGrid = show; }

  void resetRotation() { m_rotationAngle = 0.0f; }

  MeshVisual* cubeVisual() const { return m_cubeVisual.get(); }
  MeshVisual* gridVisual() const { return m_gridVisual.get(); }

private:
  IllumoContext* ic{ nullptr };
  std::unique_ptr<MeshVisual> m_cubeVisual;
  std::unique_ptr<MeshVisual> m_gridVisual;

  float m_rotationAngle{ 0.0f };
  float m_rotationSpeed{ 1.2f };
  bool m_paused{ false };
  bool m_showGrid{ true };
  std::array<bool, 5> m_keysDown{};
  std::array<bool, 5> m_keysBlocked{};
};
