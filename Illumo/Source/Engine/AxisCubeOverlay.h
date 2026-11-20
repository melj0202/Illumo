#pragma once

#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Font.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/IEnvVars.h>
#include <Illumo/Services/KeyCode.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <glm/geometric.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <string>

// Transparent centered 3D orientation cube and colored world-axis lines
// (X=red, Y=green, Z=blue) projected with the active scene camera's exact
// orientation and perspective (or orthographic) basis. Drawn on
// RenderLayerId::Debug via GameVisual so it stays see-through, post-process
// free, and identical on OpenGL and Vulkan.
class AxisCubeOverlay final : public DrawableBase
{
public:
  static constexpr float kHalfExtent = 0.5f;
  static constexpr float kPositiveAxisLength = 1.05f;
  static constexpr float kNegativeAxisLength = 0.80f;
  static constexpr float kAxisLabelDistance = 1.22f;
  // Matches Saturn's default 50-degree perspective isometric camera (distance
  // ~4.5 units) while sizing the cube cleanly at ~33% of viewport height.
  static constexpr float kTargetHalfHeight = 2.1f;
  static constexpr float kMinCameraDistance = 2.2f;

  struct ViewBasis
  {
    glm::vec3 right{ 0.70710678f, 0.0f, -0.70710678f };
    glm::vec3 up{ -0.40824829f, 0.81649658f, -0.40824829f };
    glm::vec3 forward{ -0.57735027f, -0.57735027f, -0.57735027f };
    float tanHalfFovX = 0.8289914f;
    float tanHalfFovY = 0.46630766f;
    float distance = 4.503465f;
    bool perspective = true;
  };

  struct VelocityTelemetry
  {
    glm::vec3 velocity{ 0.0f, 0.0f, 0.0f };
    float referenceSpeed = 1.0f;
    bool valid = false;
  };

  // Maps a world-space velocity vector into cube-local coordinates [-h, +h]^3:
  // points along the exact 3D velocity direction, with length reaching from the
  // cube center (speed 0) to the cube boundary (speed >= referenceSpeed).
  static glm::vec3 computeVelocityCubePoint(const glm::vec3& velocity,
                                            float referenceSpeed)
  {
    if (!isFiniteVec3(velocity) || !std::isfinite(referenceSpeed) ||
        referenceSpeed <= 1.0e-5f) {
      return glm::vec3(0.0f);
    }
    const float speed = glm::length(velocity);
    if (!std::isfinite(speed) || speed <= 1.0e-3f) {
      return glm::vec3(0.0f);
    }
    const glm::vec3 dir = velocity / speed;
    const float maxComponent =
      std::max(std::fabs(dir.x), std::max(std::fabs(dir.y), std::fabs(dir.z)));
    if (!std::isfinite(maxComponent) || maxComponent <= 1.0e-5f) {
      return glm::vec3(0.0f);
    }
    const float distToCubeBoundary = kHalfExtent / maxComponent;
    const float speedFraction = std::clamp(speed / referenceSpeed, 0.0f, 1.0f);
    return dir * (distToCubeBoundary * speedFraction);
  }

  AxisCubeOverlay()
  {
    m_visual.setSpace(PrimitiveSpace::Pixels);
    m_visual.setLayerHint(RenderLayerId::Debug);
    m_visual.setVisible(false);
  }
  AxisCubeOverlay(const AxisCubeOverlay&) = delete;
  AxisCubeOverlay& operator=(const AxisCubeOverlay&) = delete;
  AxisCubeOverlay(AxisCubeOverlay&&) = delete;
  AxisCubeOverlay& operator=(AxisCubeOverlay&&) = delete;
  ~AxisCubeOverlay() override = default;

  static bool isEnabled(IEnvVars* environment)
  {
    if (environment == nullptr) {
      return false;
    }
    const EnvVar& var = environment->getVar("showAxisCube");
    if (!var.value.empty()) {
      return var.valueAsBool;
    }
    return false;
  }

  static ViewBasis defaultViewBasis(float aspectRatio)
  {
    const float safeAspect = (std::isfinite(aspectRatio) && aspectRatio > 0.1f)
                               ? aspectRatio
                               : (16.0f / 9.0f);
    ViewBasis basis;
    basis.tanHalfFovY = 0.46630766f; // tan(25 deg), 50-degree vertical FOV
    basis.tanHalfFovX = basis.tanHalfFovY * safeAspect;
    basis.distance =
      std::max(kMinCameraDistance, kTargetHalfHeight / basis.tanHalfFovY);
    basis.perspective = true;
    return basis;
  }

  // Extracts the active camera's right/up/forward basis and perspective (or
  // orthographic) scale directly from the frame's world view-projection matrix.
  // Translation-invariant so the cube remains centered while tracking camera
  // rotations, orbits, and FOV changes.
  static ViewBasis extractViewBasis(const glm::mat4& worldMvp,
                                    float aspectRatio)
  {
    const ViewBasis fallback = defaultViewBasis(aspectRatio);
    const float det = glm::determinant(worldMvp);
    if (!std::isfinite(det) || std::fabs(det) <= 1.0e-9f) {
      return fallback;
    }

    const glm::mat4 invM = glm::inverse(worldMvp);
    glm::vec3 p00(0.0f);
    glm::vec3 pr0(0.0f);
    glm::vec3 pu0(0.0f);
    if (!unprojectNdc(invM, 0.0f, 0.0f, 0.0f, &p00) ||
        !unprojectNdc(invM, 1.0f, 0.0f, 0.0f, &pr0) ||
        !unprojectNdc(invM, 0.0f, 1.0f, 0.0f, &pu0)) {
      return fallback;
    }

    const glm::vec4 eyeH = invM * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
    const bool isPerspective =
      std::isfinite(eyeH.w) && std::fabs(eyeH.w) > 1.0e-5f;

    glm::vec3 forward(0.0f);
    float centerDistance = 0.0f;
    if (isPerspective) {
      const glm::vec3 eye = glm::vec3(eyeH) / eyeH.w;
      if (!isFiniteVec3(eye)) {
        return fallback;
      }
      const glm::vec3 fwdVec = p00 - eye;
      const float fwdLen = glm::length(fwdVec);
      if (!std::isfinite(fwdLen) || fwdLen <= 1.0e-5f) {
        return fallback;
      }
      forward = fwdVec / fwdLen;
      // Ensure forward points from near plane toward far plane.
      glm::vec3 p0n(0.0f);
      if (unprojectNdc(invM, 0.0f, 0.0f, -1.0f, &p0n) &&
          glm::dot(p00 - p0n, forward) < 0.0f) {
        forward = -forward;
      }
      centerDistance = glm::dot(p00 - eye, forward);
      if (!std::isfinite(centerDistance) || centerDistance <= 1.0e-5f) {
        return fallback;
      }
    } else {
      glm::vec3 p0n(0.0f);
      glm::vec3 p0f(0.0f);
      if (!unprojectNdc(invM, 0.0f, 0.0f, -1.0f, &p0n) ||
          !unprojectNdc(invM, 0.0f, 0.0f, 1.0f, &p0f)) {
        return fallback;
      }
      const glm::vec3 fwdVec = p0f - p0n;
      const float fwdLen = glm::length(fwdVec);
      if (!std::isfinite(fwdLen) || fwdLen <= 1.0e-5f) {
        return fallback;
      }
      forward = fwdVec / fwdLen;
    }

    const glm::vec3 rightRaw = pr0 - p00;
    const glm::vec3 rightOrtho =
      rightRaw - forward * glm::dot(rightRaw, forward);
    const float rightLen = glm::length(rightOrtho);
    if (!std::isfinite(rightLen) || rightLen <= 1.0e-5f) {
      return fallback;
    }
    const glm::vec3 right = rightOrtho / rightLen;

    const glm::vec3 upRaw = pu0 - p00;
    const glm::vec3 upOrtho = upRaw - forward * glm::dot(upRaw, forward) -
                              right * glm::dot(upRaw, right);
    const float upLen = glm::length(upOrtho);
    if (!std::isfinite(upLen) || upLen <= 1.0e-5f) {
      return fallback;
    }
    const glm::vec3 up = upOrtho / upLen;

    if (!isFiniteVec3(forward) || !isFiniteVec3(right) || !isFiniteVec3(up)) {
      return fallback;
    }

    ViewBasis basis;
    basis.right = right;
    basis.up = up;
    basis.forward = forward;
    if (isPerspective) {
      basis.perspective = true;
      basis.tanHalfFovY = std::clamp(upLen / centerDistance, 0.08f, 2.0f);
      basis.tanHalfFovX = std::clamp(rightLen / centerDistance, 0.08f, 4.0f);
      basis.distance =
        std::max(kMinCameraDistance, kTargetHalfHeight / basis.tanHalfFovY);
    } else {
      const float safeAspect =
        (std::isfinite(aspectRatio) && aspectRatio > 0.1f) ? aspectRatio
                                                           : (16.0f / 9.0f);
      basis.perspective = false;
      basis.tanHalfFovY = 0.46630766f;
      basis.tanHalfFovX = basis.tanHalfFovY * safeAspect;
      basis.distance = 4.5f;
    }
    return basis;
  }

  static void projectPoint(const ViewBasis& view,
                           const glm::vec3& point,
                           float viewportWidth,
                           float viewportHeight,
                           float* outX,
                           float* outY,
                           float* outDepth = nullptr)
  {
    const float cx = glm::dot(point, view.right);
    const float cy = glm::dot(point, view.up);
    const float depth = view.distance + glm::dot(point, view.forward);
    float ndcX = 0.0f;
    float ndcY = 0.0f;
    if (view.perspective) {
      const float safeDepth = std::max(depth, 0.1f);
      ndcX = cx / (safeDepth * view.tanHalfFovX);
      ndcY = cy / (safeDepth * view.tanHalfFovY);
    } else {
      const float safeAspect = viewportHeight > 0.0f
                                 ? (viewportWidth / viewportHeight)
                                 : (16.0f / 9.0f);
      ndcX = cx / (kTargetHalfHeight * safeAspect);
      ndcY = cy / kTargetHalfHeight;
    }
    if (outX != nullptr) {
      *outX = (0.5f + 0.5f * ndcX) * viewportWidth;
    }
    if (outY != nullptr) {
      *outY = (0.5f - 0.5f * ndcY) * viewportHeight;
    }
    if (outDepth != nullptr) {
      *outDepth = depth;
    }
  }

  void prepare(Renderer* renderer, IRenderWindow* window, Camera* camera)
  {
    m_renderer = renderer;
    m_window = window;
    m_camera = camera;
    m_visual.setWindow(window);
    m_visual.setCamera(camera);
    m_visual.prepare(renderer);
  }

  bool visible() const { return m_enabled; }

  void setEnabled(bool enabled, IEnvVars* environment = nullptr)
  {
    m_enabled = enabled;
    if (environment != nullptr) {
      environment->setVar("showAxisCube", enabled);
    }
    if (!m_enabled) {
      m_visual.clearPrimitives();
      m_visual.setVisible(false);
    } else {
      m_visual.setVisible(true);
    }
  }

  void syncFromEnv(IEnvVars* environment)
  {
    if (environment == nullptr) {
      return;
    }
    const EnvVar& var = environment->getVar("showAxisCube");
    if (var.value.empty()) {
      return;
    }
    if (var.valueAsBool != m_enabled) {
      m_enabled = var.valueAsBool;
      if (!m_enabled) {
        m_visual.clearPrimitives();
        m_visual.setVisible(false);
      } else {
        m_visual.setVisible(true);
      }
    }
  }

  bool handleKey(KeyCode key,
                 InputAction action,
                 bool consoleOpen,
                 bool hasModifier,
                 bool f8BoundByUser,
                 IEnvVars* environment)
  {
    if (consoleOpen || hasModifier) {
      return false;
    }
    if (key == KeyCode::F8 && !f8BoundByUser) {
      if (action == InputAction::Press) {
        setEnabled(!m_enabled, environment);
      }
      return true;
    }
    return false;
  }

  void rebuild(const glm::mat4& worldMvp,
               float viewportWidth,
               float viewportHeight,
               const VelocityTelemetry* velocity = nullptr)
  {
    const float width = std::max(1.0f, viewportWidth);
    const float height = std::max(1.0f, viewportHeight);
    m_lastView = extractViewBasis(worldMvp, width / height);
    m_lastVelocity = velocity != nullptr ? *velocity : VelocityTelemetry{};
    composeCube(m_lastView, m_lastVelocity, width, height);
  }

  bool AppendCommands(Renderer* renderer) override
  {
    if (!m_enabled) {
      return true;
    }
    if (renderer == nullptr) {
      return false;
    }

    const Renderer::FrameContext& fc = renderer->getFrameContext();
    float pixelW = 1280.0f;
    float pixelH = 720.0f;
    float uiScale = 1.0f;
    if (fc.active && fc.windowDimensions[0] > 0 && fc.windowDimensions[1] > 0) {
      pixelW = static_cast<float>(fc.windowDimensions[0]);
      pixelH = static_cast<float>(fc.windowDimensions[1]);
      uiScale = fc.uiScale > 0.0f ? fc.uiScale : 1.0f;
    } else if (m_window != nullptr) {
      const std::array<int, 2> dims = m_window->getWindowDimensions();
      if (dims[0] > 0 && dims[1] > 0) {
        pixelW = static_cast<float>(dims[0]);
        pixelH = static_cast<float>(dims[1]);
      }
      const float scale = renderer->getUiScale();
      uiScale = scale > 0.0f ? scale : 1.0f;
    }
    const float logicalW = std::max(1.0f, pixelW / uiScale);
    const float logicalH = std::max(1.0f, pixelH / uiScale);
    const float aspect = logicalW / logicalH;

    if (fc.active && fc.hasWorldMvp) {
      glm::mat4 worldMvp(1.0f);
      std::memcpy(&worldMvp[0][0],
                  fc.worldMvp.data(),
                  fc.worldMvp.size() * sizeof(float));
      m_lastView = extractViewBasis(worldMvp, aspect);
    } else if (m_camera != nullptr) {
      m_lastView = extractViewBasis(m_camera->GetMVPMatrix(aspect), aspect);
    } else {
      m_lastView = defaultViewBasis(aspect);
    }

    m_lastVelocity = VelocityTelemetry{};
    if (fc.active && fc.hasDebugVelocity) {
      m_lastVelocity.velocity = glm::vec3(
        fc.debugVelocity[0], fc.debugVelocity[1], fc.debugVelocity[2]);
      m_lastVelocity.referenceSpeed = fc.debugVelocityReferenceSpeed;
      m_lastVelocity.valid = true;
    }

    composeCube(m_lastView, m_lastVelocity, logicalW, logicalH);
    m_visual.setRenderer(renderer);
    m_visual.setWindow(m_window);
    m_visual.setSpace(PrimitiveSpace::Pixels);
    m_visual.setVisible(true);
    return m_visual.AppendCommands(renderer);
  }

  void Draw() override {}

  const GameVisual& visual() const { return m_visual; }
  const ViewBasis& lastView() const { return m_lastView; }
  const VelocityTelemetry& lastVelocity() const { return m_lastVelocity; }

private:
  struct ScreenPoint
  {
    float x = 0.0f;
    float y = 0.0f;
    float depth = 0.0f;
  };

  struct FaceDef
  {
    std::array<std::size_t, 4> corners;
    glm::vec3 normal;
  };

  struct EdgeDef
  {
    std::size_t a;
    std::size_t b;
    std::size_t faceA;
    std::size_t faceB;
  };

  struct HalfAxisDef
  {
    glm::vec3 dir;
    std::size_t faceIndex;
    ColorRgba baseColor;
    const char* label;
    bool positive;
  };

  static bool isFiniteVec3(const glm::vec3& v)
  {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
  }

  static bool unprojectNdc(const glm::mat4& invM,
                           float ndcX,
                           float ndcY,
                           float ndcZ,
                           glm::vec3* outPos)
  {
    const glm::vec4 h = invM * glm::vec4(ndcX, ndcY, ndcZ, 1.0f);
    if (!std::isfinite(h.w) || std::fabs(h.w) <= 1.0e-6f) {
      return false;
    }
    const glm::vec3 p = glm::vec3(h) / h.w;
    if (!isFiniteVec3(p)) {
      return false;
    }
    *outPos = p;
    return true;
  }

  static std::uint8_t clampByte(float v)
  {
    const float clamped = std::clamp(v, 0.0f, 255.0f);
    return static_cast<std::uint8_t>(clamped + 0.5f);
  }

  void addAxisArrow(const ScreenPoint& from,
                    const ScreenPoint& to,
                    ColorRgba color,
                    float lineWidth,
                    bool withArrowhead)
  {
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float len = std::hypot(dx, dy);
    if (!withArrowhead) {
      if (len > 0.5f) {
        m_visual.addLine(from.x, from.y, to.x, to.y, color, lineWidth);
      }
      return;
    }
    if (len <= 6.0f) {
      if (len > 0.5f) {
        m_visual.addLine(from.x, from.y, to.x, to.y, color, lineWidth);
      }
      const float r = 3.5f;
      m_visual.addFilledEllipse(to.x - r, to.y - r, r * 2.0f, r * 2.0f, color);
      return;
    }
    const float ux = dx / len;
    const float uy = dy / len;
    const float px = -uy;
    const float py = ux;
    const float ah = std::min(10.0f, len * 0.35f);
    const float aw = ah * 0.46f;
    const float shaftEndX = to.x - ux * (ah * 0.65f);
    const float shaftEndY = to.y - uy * (ah * 0.65f);
    m_visual.addLine(from.x, from.y, shaftEndX, shaftEndY, color, lineWidth);
    m_visual.addFilledTriangle(to.x,
                               to.y,
                               to.x - ux * ah + px * aw,
                               to.y - uy * ah + py * aw,
                               to.x - ux * ah - px * aw,
                               to.y - uy * ah - py * aw,
                               color);
  }

  void composeCube(const ViewBasis& view,
                   const VelocityTelemetry& velocity,
                   float viewportWidth,
                   float viewportHeight)
  {
    m_visual.clearPrimitives();

    static constexpr float h = kHalfExtent;
    const std::array<glm::vec3, 8> corners = {
      glm::vec3(-h, -h, -h), glm::vec3(h, -h, -h), glm::vec3(-h, h, -h),
      glm::vec3(h, h, -h),   glm::vec3(-h, -h, h), glm::vec3(h, -h, h),
      glm::vec3(-h, h, h),   glm::vec3(h, h, h)
    };
    const std::array<FaceDef, 6> faces = {
      FaceDef{ { 1, 3, 7, 5 }, glm::vec3(1.0f, 0.0f, 0.0f) },
      FaceDef{ { 0, 4, 6, 2 }, glm::vec3(-1.0f, 0.0f, 0.0f) },
      FaceDef{ { 2, 6, 7, 3 }, glm::vec3(0.0f, 1.0f, 0.0f) },
      FaceDef{ { 0, 1, 5, 4 }, glm::vec3(0.0f, -1.0f, 0.0f) },
      FaceDef{ { 4, 5, 7, 6 }, glm::vec3(0.0f, 0.0f, 1.0f) },
      FaceDef{ { 0, 2, 3, 1 }, glm::vec3(0.0f, 0.0f, -1.0f) }
    };
    const std::array<EdgeDef, 12> edges = {
      EdgeDef{ 0, 1, 3, 5 }, EdgeDef{ 2, 3, 2, 5 }, EdgeDef{ 4, 5, 3, 4 },
      EdgeDef{ 6, 7, 2, 4 }, EdgeDef{ 0, 2, 1, 5 }, EdgeDef{ 1, 3, 0, 5 },
      EdgeDef{ 4, 6, 1, 4 }, EdgeDef{ 5, 7, 0, 4 }, EdgeDef{ 0, 4, 1, 3 },
      EdgeDef{ 1, 5, 0, 3 }, EdgeDef{ 2, 6, 1, 2 }, EdgeDef{ 3, 7, 0, 2 }
    };
    const std::array<HalfAxisDef, 6> halfAxes = {
      HalfAxisDef{ glm::vec3(1.0f, 0.0f, 0.0f),
                   0,
                   ColorRgba{ 248, 86, 86, 255 },
                   "X",
                   true },
      HalfAxisDef{ glm::vec3(-1.0f, 0.0f, 0.0f),
                   1,
                   ColorRgba{ 248, 86, 86, 255 },
                   nullptr,
                   false },
      HalfAxisDef{ glm::vec3(0.0f, 1.0f, 0.0f),
                   2,
                   ColorRgba{ 90, 228, 120, 255 },
                   "Y",
                   true },
      HalfAxisDef{ glm::vec3(0.0f, -1.0f, 0.0f),
                   3,
                   ColorRgba{ 90, 228, 120, 255 },
                   nullptr,
                   false },
      HalfAxisDef{ glm::vec3(0.0f, 0.0f, 1.0f),
                   4,
                   ColorRgba{ 92, 168, 255, 255 },
                   "Z",
                   true },
      HalfAxisDef{ glm::vec3(0.0f, 0.0f, -1.0f),
                   5,
                   ColorRgba{ 92, 168, 255, 255 },
                   nullptr,
                   false }
    };

    std::array<ScreenPoint, 8> projectedCorners{};
    for (std::size_t i = 0; i < corners.size(); ++i) {
      projectPoint(view,
                   corners[i],
                   viewportWidth,
                   viewportHeight,
                   &projectedCorners[i].x,
                   &projectedCorners[i].y,
                   &projectedCorners[i].depth);
    }

    ScreenPoint originPt;
    projectPoint(view,
                 glm::vec3(0.0f, 0.0f, 0.0f),
                 viewportWidth,
                 viewportHeight,
                 &originPt.x,
                 &originPt.y,
                 &originPt.depth);

    const glm::vec3 eyePos = -view.forward * view.distance;
    std::array<bool, 6> faceFront{};
    std::array<float, 6> faceDepth{};
    std::array<std::size_t, 6> faceOrder = { 0, 1, 2, 3, 4, 5 };
    for (std::size_t i = 0; i < faces.size(); ++i) {
      const glm::vec3 center = faces[i].normal * h;
      const glm::vec3 toEye =
        view.perspective ? (eyePos - center) : (-view.forward);
      faceFront[i] = glm::dot(faces[i].normal, toEye) > 0.0f;
      faceDepth[i] = view.distance + glm::dot(center, view.forward);
    }
    std::sort(faceOrder.begin(),
              faceOrder.end(),
              [&faceDepth](std::size_t a, std::size_t b) {
                return faceDepth[a] > faceDepth[b];
              });

    std::array<ScreenPoint, 6> axisFacePts{};
    std::array<ScreenPoint, 6> axisTipPts{};
    std::array<float, 6> interiorDepth{};
    std::array<float, 6> exteriorDepth{};
    for (std::size_t i = 0; i < halfAxes.size(); ++i) {
      const glm::vec3 facePos = halfAxes[i].dir * h;
      const float tipLen =
        halfAxes[i].positive ? kPositiveAxisLength : kNegativeAxisLength;
      const glm::vec3 tipPos = halfAxes[i].dir * tipLen;
      projectPoint(view,
                   facePos,
                   viewportWidth,
                   viewportHeight,
                   &axisFacePts[i].x,
                   &axisFacePts[i].y,
                   &axisFacePts[i].depth);
      projectPoint(view,
                   tipPos,
                   viewportWidth,
                   viewportHeight,
                   &axisTipPts[i].x,
                   &axisTipPts[i].y,
                   &axisTipPts[i].depth);
      interiorDepth[i] =
        view.distance + glm::dot(halfAxes[i].dir * (0.5f * h), view.forward);
      exteriorDepth[i] =
        view.distance +
        glm::dot(halfAxes[i].dir * (0.5f * (h + tipLen)), view.forward);
    }

    std::array<std::size_t, 6> extOrder = { 0, 1, 2, 3, 4, 5 };
    std::sort(extOrder.begin(),
              extOrder.end(),
              [&exteriorDepth](std::size_t a, std::size_t b) {
                return exteriorDepth[a] > exteriorDepth[b];
              });

    std::array<std::size_t, 6> intOrder = { 0, 1, 2, 3, 4, 5 };
    std::sort(intOrder.begin(),
              intOrder.end(),
              [&interiorDepth](std::size_t a, std::size_t b) {
                return interiorDepth[a] > interiorDepth[b];
              });

    const glm::vec3 lightDir = glm::normalize(
      view.right * 0.35f + view.up * 0.70f - view.forward * 0.62f);

    // 1. Exterior half-axes emerging from back-facing cube faces (behind cube).
    for (std::size_t idx : extOrder) {
      if (faceFront[halfAxes[idx].faceIndex]) {
        continue;
      }
      ColorRgba col = halfAxes[idx].baseColor;
      col.a = halfAxes[idx].positive ? 175 : 85;
      addAxisArrow(axisFacePts[idx],
                   axisTipPts[idx],
                   col,
                   halfAxes[idx].positive ? 2.6f : 1.3f,
                   halfAxes[idx].positive);
    }

    // 2. Back faces of the transparent cube (far-to-near).
    for (std::size_t fi : faceOrder) {
      if (faceFront[fi]) {
        continue;
      }
      const float ndotl = std::clamp(
        glm::dot(faces[fi].normal, lightDir) * 0.5f + 0.5f, 0.0f, 1.0f);
      const ColorRgba fill{ clampByte(165.0f + 35.0f * ndotl),
                            clampByte(185.0f + 30.0f * ndotl),
                            clampByte(215.0f + 20.0f * ndotl),
                            clampByte(12.0f + 10.0f * ndotl) };
      const std::array<std::size_t, 4>& c = faces[fi].corners;
      m_visual.addGradientQuad(projectedCorners[c[0]].x,
                               projectedCorners[c[0]].y,
                               projectedCorners[c[1]].x,
                               projectedCorners[c[1]].y,
                               projectedCorners[c[2]].x,
                               projectedCorners[c[2]].y,
                               projectedCorners[c[3]].x,
                               projectedCorners[c[3]].y,
                               fill,
                               fill,
                               fill,
                               fill);
    }

    // 3. Occluded back edges of the cube (faint so 3D depth reads cleanly).
    const ColorRgba backEdgeColor{ 195, 210, 232, 85 };
    for (const EdgeDef& edge : edges) {
      if (faceFront[edge.faceA] || faceFront[edge.faceB]) {
        continue;
      }
      m_visual.addLine(projectedCorners[edge.a].x,
                       projectedCorners[edge.a].y,
                       projectedCorners[edge.b].x,
                       projectedCorners[edge.b].y,
                       backEdgeColor,
                       1.2f);
    }

    // 4. Interior half-axes from origin to cube faces (sorted far-to-near) with
    //    the origin dot drawn between receding and approaching axes.
    bool originDrawn = false;
    const ColorRgba originColor{ 245, 248, 255, 215 };
    for (std::size_t idx : intOrder) {
      if (!originDrawn && interiorDepth[idx] < originPt.depth) {
        m_visual.addFilledEllipse(
          originPt.x - 2.5f, originPt.y - 2.5f, 5.0f, 5.0f, originColor);
        originDrawn = true;
      }
      ColorRgba col = halfAxes[idx].baseColor;
      col.a = halfAxes[idx].positive ? 205 : 105;
      m_visual.addLine(originPt.x,
                       originPt.y,
                       axisFacePts[idx].x,
                       axisFacePts[idx].y,
                       col,
                       halfAxes[idx].positive ? 2.4f : 1.3f);
    }
    if (!originDrawn) {
      m_visual.addFilledEllipse(
        originPt.x - 2.5f, originPt.y - 2.5f, 5.0f, 5.0f, originColor);
    }

    // 4b. Player velocity vector inside the cube (origin -> scaled tip up to
    //     cube boundary), drawn above interior axes and beneath front glass.
    if (velocity.valid) {
      const glm::vec3 velPoint =
        computeVelocityCubePoint(velocity.velocity, velocity.referenceSpeed);
      if (glm::length(velPoint) > 1.0e-3f) {
        ScreenPoint velTipPt;
        projectPoint(view,
                     velPoint,
                     viewportWidth,
                     viewportHeight,
                     &velTipPt.x,
                     &velTipPt.y,
                     &velTipPt.depth);
        const ColorRgba velOutline{ 14, 16, 22, 195 };
        const ColorRgba velColor{ 255, 224, 48, 250 };
        const float vdx = velTipPt.x - originPt.x;
        const float vdy = velTipPt.y - originPt.y;
        const float vlen = std::hypot(vdx, vdy);
        if (vlen <= 6.0f) {
          if (vlen > 0.5f) {
            m_visual.addLine(
              originPt.x, originPt.y, velTipPt.x, velTipPt.y, velOutline, 5.0f);
            m_visual.addLine(
              originPt.x, originPt.y, velTipPt.x, velTipPt.y, velColor, 3.0f);
          }
          m_visual.addFilledEllipse(
            velTipPt.x - 5.0f, velTipPt.y - 5.0f, 10.0f, 10.0f, velOutline);
          m_visual.addFilledEllipse(
            velTipPt.x - 3.5f, velTipPt.y - 3.5f, 7.0f, 7.0f, velColor);
        } else {
          const float ux = vdx / vlen;
          const float uy = vdy / vlen;
          const float px = -uy;
          const float py = ux;
          const float ah = std::min(12.0f, vlen * 0.36f);
          const float aw = ah * 0.48f;
          const float shaftEndX = velTipPt.x - ux * (ah * 0.65f);
          const float shaftEndY = velTipPt.y - uy * (ah * 0.65f);
          m_visual.addLine(
            originPt.x, originPt.y, shaftEndX, shaftEndY, velOutline, 5.2f);
          m_visual.addFilledTriangle(
            velTipPt.x + ux * 1.6f,
            velTipPt.y + uy * 1.6f,
            velTipPt.x - ux * (ah + 1.2f) + px * (aw + 1.6f),
            velTipPt.y - uy * (ah + 1.2f) + py * (aw + 1.6f),
            velTipPt.x - ux * (ah + 1.2f) - px * (aw + 1.6f),
            velTipPt.y - uy * (ah + 1.2f) - py * (aw + 1.6f),
            velOutline);
          m_visual.addLine(
            originPt.x, originPt.y, shaftEndX, shaftEndY, velColor, 3.2f);
          m_visual.addFilledTriangle(velTipPt.x,
                                     velTipPt.y,
                                     velTipPt.x - ux * ah + px * aw,
                                     velTipPt.y - uy * ah + py * aw,
                                     velTipPt.x - ux * ah - px * aw,
                                     velTipPt.y - uy * ah - py * aw,
                                     velColor);
        }
        m_visual.addFilledEllipse(
          originPt.x - 3.0f, originPt.y - 3.0f, 6.0f, 6.0f, velColor);
      }
    }

    // 5. Front faces of the transparent cube (subtle glass glaze over
    // interior).
    for (std::size_t fi : faceOrder) {
      if (!faceFront[fi]) {
        continue;
      }
      const float ndotl = std::clamp(
        glm::dot(faces[fi].normal, lightDir) * 0.5f + 0.5f, 0.0f, 1.0f);
      const ColorRgba fill{ clampByte(195.0f + 45.0f * ndotl),
                            clampByte(212.0f + 38.0f * ndotl),
                            clampByte(235.0f + 20.0f * ndotl),
                            clampByte(18.0f + 18.0f * ndotl) };
      const std::array<std::size_t, 4>& c = faces[fi].corners;
      m_visual.addGradientQuad(projectedCorners[c[0]].x,
                               projectedCorners[c[0]].y,
                               projectedCorners[c[1]].x,
                               projectedCorners[c[1]].y,
                               projectedCorners[c[2]].x,
                               projectedCorners[c[2]].y,
                               projectedCorners[c[3]].x,
                               projectedCorners[c[3]].y,
                               fill,
                               fill,
                               fill,
                               fill);
    }

    // 6. Front / silhouette edges of the cube.
    const ColorRgba frontEdgeColor{ 238, 245, 255, 205 };
    for (const EdgeDef& edge : edges) {
      if (!faceFront[edge.faceA] && !faceFront[edge.faceB]) {
        continue;
      }
      m_visual.addLine(projectedCorners[edge.a].x,
                       projectedCorners[edge.a].y,
                       projectedCorners[edge.b].x,
                       projectedCorners[edge.b].y,
                       frontEdgeColor,
                       1.8f);
    }

    // 7. Exterior half-axes emerging from front-facing cube faces.
    for (std::size_t idx : extOrder) {
      if (!faceFront[halfAxes[idx].faceIndex]) {
        continue;
      }
      ColorRgba col = halfAxes[idx].baseColor;
      col.a = halfAxes[idx].positive ? 245 : 120;
      addAxisArrow(axisFacePts[idx],
                   axisTipPts[idx],
                   col,
                   halfAxes[idx].positive ? 2.8f : 1.4f,
                   halfAxes[idx].positive);
    }

    // 8. Colored X / Y / Z axis labels at each positive axis tip.
    const float labelPt = 16.0f;
    const std::shared_ptr<Font> font = Font::getDefaultFont();
    for (std::size_t idx : extOrder) {
      if (!halfAxes[idx].positive || halfAxes[idx].label == nullptr) {
        continue;
      }
      ScreenPoint labelPtPos;
      projectPoint(view,
                   halfAxes[idx].dir * kAxisLabelDistance,
                   viewportWidth,
                   viewportHeight,
                   &labelPtPos.x,
                   &labelPtPos.y,
                   &labelPtPos.depth);
      float textW = labelPt * 0.6f;
      float textH = labelPt;
      if (font != nullptr) {
        const TextBounds bounds =
          font->measureText(halfAxes[idx].label, labelPt);
        textW = bounds.width;
        textH = bounds.height;
      }
      const float tx = labelPtPos.x - textW * 0.5f;
      const float ty = labelPtPos.y - textH * 0.5f;
      const bool inFront = faceFront[halfAxes[idx].faceIndex];
      const ColorRgba shadow{ 12,
                              16,
                              24,
                              inFront ? static_cast<std::uint8_t>(215)
                                      : static_cast<std::uint8_t>(155) };
      ColorRgba textCol = halfAxes[idx].baseColor;
      textCol.a = inFront ? 250 : 185;
      m_visual.addText(
        halfAxes[idx].label, tx + 1.0f, ty + 1.0f, labelPt, shadow);
      m_visual.addText(halfAxes[idx].label, tx, ty, labelPt, textCol);
    }
  }

  Renderer* m_renderer = nullptr;
  IRenderWindow* m_window = nullptr;
  Camera* m_camera = nullptr;
  GameVisual m_visual;
  ViewBasis m_lastView{};
  VelocityTelemetry m_lastVelocity{};
  bool m_enabled = false;
};
