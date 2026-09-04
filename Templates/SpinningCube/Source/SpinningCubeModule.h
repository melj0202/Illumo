#pragma once

#include <Illumo/Engine/IModule.h>
#include <Illumo/Rendering/Primitives/MeshVisual.h>
#include <memory>

struct IllumoContext;

class SpinningCubeModule : public IModule
{
public:
  SpinningCubeModule();
  ~SpinningCubeModule() override;

  SpinningCubeModule(const SpinningCubeModule&) = delete;
  SpinningCubeModule& operator=(const SpinningCubeModule&) = delete;
  SpinningCubeModule(SpinningCubeModule&&) = delete;
  SpinningCubeModule& operator=(SpinningCubeModule&&) = delete;

  bool Start(IllumoContext* context) override;
  void Update(double dt) override;
  void DispatchDrawables(Scene* scene) override;
  void Exit() override;

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
};
