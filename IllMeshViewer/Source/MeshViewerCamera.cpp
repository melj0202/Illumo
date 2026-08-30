#include "MeshViewerCamera.h"

#include <Illumo/Rendering/Camera.h>
#include <algorithm>
#include <cmath>

static constexpr float kDefaultYaw = 0.78539816339f;   // 45 degrees
static constexpr float kDefaultPitch = 0.43633231299f; // 25 degrees
static constexpr float kDefaultDistance = 3.5f;
static constexpr float kMinPitch = -1.50f;
static constexpr float kMaxPitch = 1.50f;
static constexpr float kMinDistance = 0.05f;
static constexpr float kMaxDistance = 2000.0f;

MeshViewerCamera::MeshViewerCamera()
  : m_target(0.0f, 0.0f, 0.0f)
  , m_targetTarget(0.0f, 0.0f, 0.0f)
  , m_distance(kDefaultDistance)
  , m_targetDistance(kDefaultDistance)
  , m_yaw(kDefaultYaw)
  , m_targetYaw(kDefaultYaw)
  , m_pitch(kDefaultPitch)
  , m_targetPitch(kDefaultPitch)
  , m_roll(0.0f)
  , m_targetRoll(0.0f)
  , m_fov(50.0f)
  , m_nearPlane(0.05f)
  , m_farPlane(2000.0f)
  , m_smoothingSpeed(18.0f)
{
}

void
MeshViewerCamera::update(float dt)
{
  if (dt <= 0.0f) {
    return;
  }
  const float t = std::min(1.0f, dt * m_smoothingSpeed);

  m_target += (m_targetTarget - m_target) * t;
  m_distance = glm::mix(m_distance, m_targetDistance, t);
  m_yaw = glm::mix(m_yaw, m_targetYaw, t);
  m_pitch = glm::mix(m_pitch, m_targetPitch, t);
  m_roll = glm::mix(m_roll, m_targetRoll, t);
}

void
MeshViewerCamera::applyTo(Camera* camera)
{
  if (camera == nullptr) {
    return;
  }
  const glm::vec3 eye = computeEye();
  const glm::vec3 up = computeUp();

  camera->lookAt(eye, m_target, up);
  camera->setPerspective(m_fov, m_nearPlane, m_farPlane);
  camera->setProjectionType(ProjectionType::Perspective);
}

void
MeshViewerCamera::orbit(float deltaYaw, float deltaPitch)
{
  m_targetYaw += deltaYaw;
  m_targetPitch = std::clamp(m_targetPitch + deltaPitch, kMinPitch, kMaxPitch);
}

void
MeshViewerCamera::pan(float deltaRight, float deltaUp)
{
  const glm::vec3 right = computeRight();
  const glm::vec3 up = computeUp();
  m_targetTarget += (-deltaRight * right + deltaUp * up);
}

void
MeshViewerCamera::panWorld(const glm::vec3& deltaWorld)
{
  m_targetTarget += deltaWorld;
}

void
MeshViewerCamera::zoom(float factor)
{
  m_targetDistance =
    std::clamp(m_targetDistance * factor, kMinDistance, kMaxDistance);
}

void
MeshViewerCamera::rotate(float deltaRoll)
{
  m_targetRoll += deltaRoll;
}

void
MeshViewerCamera::frameBounds(const glm::vec3& minBounds,
                              const glm::vec3& maxBounds)
{
  const glm::vec3 center = (minBounds + maxBounds) * 0.5f;
  const glm::vec3 extents = maxBounds - minBounds;
  const float maxDim = std::max(extents.x, std::max(extents.y, extents.z));
  const float radius = maxDim > 0.001f ? maxDim * 0.5f : 1.0f;

  m_targetTarget = center;
  m_targetDistance = std::max(1.5f, radius * 2.5f);
  m_targetYaw = kDefaultYaw;
  m_targetPitch = kDefaultPitch;
  m_targetRoll = 0.0f;
}

void
MeshViewerCamera::reset()
{
  m_targetTarget = glm::vec3(0.0f, 0.0f, 0.0f);
  m_targetDistance = kDefaultDistance;
  m_targetYaw = kDefaultYaw;
  m_targetPitch = kDefaultPitch;
  m_targetRoll = 0.0f;
}

void
MeshViewerCamera::setTarget(const glm::vec3& target)
{
  m_target = target;
  m_targetTarget = target;
}

void
MeshViewerCamera::setDistance(float distance)
{
  m_distance = std::clamp(distance, kMinDistance, kMaxDistance);
  m_targetDistance = m_distance;
}

void
MeshViewerCamera::setYaw(float yaw)
{
  m_yaw = yaw;
  m_targetYaw = yaw;
}

void
MeshViewerCamera::setPitch(float pitch)
{
  m_pitch = std::clamp(pitch, kMinPitch, kMaxPitch);
  m_targetPitch = m_pitch;
}

void
MeshViewerCamera::setRoll(float roll)
{
  m_roll = roll;
  m_targetRoll = roll;
}

glm::vec3
MeshViewerCamera::computeEye() const
{
  const glm::vec3 offset(std::cos(m_pitch) * std::sin(m_yaw),
                         std::sin(m_pitch),
                         std::cos(m_pitch) * std::cos(m_yaw));
  return m_target + offset * m_distance;
}

glm::vec3
MeshViewerCamera::computeForward() const
{
  const glm::vec3 eye = computeEye();
  const glm::vec3 dir = m_target - eye;
  const float len = glm::length(dir);
  return len > 0.0001f ? dir / len : glm::vec3(0.0f, 0.0f, -1.0f);
}

glm::vec3
MeshViewerCamera::computeRight() const
{
  const glm::vec3 forward = computeForward();
  const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
  const glm::vec3 right = glm::cross(forward, worldUp);
  const float len = glm::length(right);
  if (len > 0.0001f) {
    return right / len;
  }
  return glm::vec3(1.0f, 0.0f, 0.0f);
}

glm::vec3
MeshViewerCamera::computeUp() const
{
  const glm::vec3 forward = computeForward();
  const glm::vec3 right = computeRight();
  glm::vec3 up = glm::cross(right, forward);
  const float len = glm::length(up);
  if (len > 0.0001f) {
    up /= len;
  } else {
    up = glm::vec3(0.0f, 1.0f, 0.0f);
  }

  if (std::abs(m_roll) > 0.0001f) {
    const glm::mat4 rot = glm::rotate(glm::mat4(1.0f), m_roll, forward);
    up = glm::vec3(rot * glm::vec4(up, 0.0f));
  }
  return up;
}

glm::mat4
MeshViewerCamera::getViewMatrix() const
{
  return glm::lookAt(computeEye(), m_target, computeUp());
}

glm::mat4
MeshViewerCamera::getProjectionMatrix(float aspectRatio) const
{
  const float aspect = aspectRatio > 0.0f ? aspectRatio : 1.0f;
  return glm::perspective(glm::radians(m_fov), aspect, m_nearPlane, m_farPlane);
}
