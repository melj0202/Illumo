#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera;

class MeshViewerCamera
{
public:
  MeshViewerCamera();
  ~MeshViewerCamera() = default;

  void update(float dt);
  void applyTo(Camera* camera);

  void orbit(float deltaYaw, float deltaPitch);
  void pan(float deltaRight, float deltaUp);
  void panWorld(const glm::vec3& deltaWorld);
  void zoom(float factor);
  void rotate(float deltaRoll);

  void frameBounds(const glm::vec3& minBounds, const glm::vec3& maxBounds);
  void reset();

  void setSmoothingSpeed(float speed) { m_smoothingSpeed = speed; }
  float smoothingSpeed() const { return m_smoothingSpeed; }

  void setFov(float degrees) { m_fov = degrees; }
  float fov() const { return m_fov; }

  const glm::vec3& target() const { return m_target; }
  void setTarget(const glm::vec3& target);

  float distance() const { return m_distance; }
  void setDistance(float distance);

  float yaw() const { return m_yaw; }
  void setYaw(float yaw);

  float pitch() const { return m_pitch; }
  void setPitch(float pitch);

  float roll() const { return m_roll; }
  void setRoll(float roll);

  glm::vec3 computeEye() const;
  glm::vec3 computeUp() const;
  glm::vec3 computeForward() const;
  glm::vec3 computeRight() const;

  glm::mat4 getViewMatrix() const;
  glm::mat4 getProjectionMatrix(float aspectRatio) const;

private:
  glm::vec3 m_target;
  glm::vec3 m_targetTarget;

  float m_distance;
  float m_targetDistance;

  float m_yaw;
  float m_targetYaw;

  float m_pitch;
  float m_targetPitch;

  float m_roll;
  float m_targetRoll;

  float m_fov;
  float m_nearPlane;
  float m_farPlane;
  float m_smoothingSpeed;
};
