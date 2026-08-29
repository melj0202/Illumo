#pragma once

#include <Illumo/Services/IEnvVars.h>
#include <array>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class IRenderWindow;

enum class ProjectionType
{
  Orthographic,
  Perspective
};

// Standalone camera (no longer inherits unused SceneObject graph — D-E4).
// Orthographic is the CA default: vec2 pan/zoom with Y-up world XY. Perspective
// uses look-at plus FOV; 2D pan state is preserved and unused until restored.
class Camera
{
public:
  Camera(const glm::vec2& initialPos = glm::vec2(0.0f, 0.0f),
         float initialZoom = 1.0f,
         IEnvVars* vars = nullptr);
  ~Camera() = default;

  // Smooth update towards target pan and zoom
  void Update(float deltaTime);

  // Camera actions
  void Pan(const glm::vec2& offset);
  void Pan(const glm::dvec2& offset);
  void ZoomAt(float zoomFactor, const glm::vec2& zoomCenter);
  void ZoomAt(float zoomFactor, const glm::dvec2& zoomCenter);
  void Rotate(float angle);
  void Reset();

  // Getters and Setters
  void SetPosition(const glm::vec2& pos)
  {
    SetPositionPrecise(static_cast<double>(pos.x), static_cast<double>(pos.y));
  }
  glm::vec2 GetPosition() const
  {
    return glm::vec2(static_cast<float>(position.x),
                     static_cast<float>(position.y));
  }
  void SetPositionPrecise(double x, double y)
  {
    targetPosition = glm::dvec2(x, y);
    position = targetPosition;
  }
  glm::dvec2 GetPositionPrecise() const { return position; }

  void SetZoom(float z)
  {
    targetZoom = z;
    zoom = z;
  }
  float GetZoom() const { return zoom; }

  void SetSmoothingSpeed(float speed) { smoothingSpeed = speed; }
  float GetSmoothingSpeed() const { return smoothingSpeed; }

  void setProjectionType(ProjectionType type) { projectionType = type; }
  ProjectionType getProjectionType() const { return projectionType; }

  void lookAt(const glm::vec3& eyePos,
              const glm::vec3& targetPos,
              const glm::vec3& upDir);
  void setPerspective(float fovDegrees, float nearPlane, float farPlane);
  const glm::vec3& getEye() const { return eye; }
  const glm::vec3& getTarget() const { return target; }
  const glm::vec3& getUp() const { return up; }
  float getFieldOfViewDegrees() const { return fieldOfViewDegrees; }

  // Coordinate conversion
  // Converts screen space [0, windowSize] to world space [-1, 1] or grid
  // coordinates. Orthographic only; perspective does not change CA picking.
  glm::vec2 ScreenToWorld(const glm::vec2& screenPos) const;
  glm::dvec2 ScreenToWorldPrecise(const glm::dvec2& screenPos) const;

  // Matrix calculations
  glm::mat4 GetViewMatrix() const;
  glm::mat4 GetProjectionMatrix(float aspectRatio) const;
  glm::mat4 GetMVPMatrix(float aspectRatio) const;

private:
  std::array<int, 2> GetWinDims() const;
  ProjectionType projectionType;
  glm::dvec2 position;       // Current interpolated position
  glm::dvec2 targetPosition; // Target position we pan towards
  float zoom;                // Current interpolated zoom
  float targetZoom;          // Target zoom we scale towards
  float smoothingSpeed; // Speed of interpolation (higher = faster, e.g. 10.0f)
  float rotation;       // Current interpolated rotation
  float targetRotation; // Target rotation we rotate towards
  IEnvVars* envVars;
  glm::vec3 eye;
  glm::vec3 target;
  glm::vec3 up;
  float fieldOfViewDegrees;
  float perspectiveNear;
  float perspectiveFar;
  float orthographicNear;
  float orthographicFar;
};
