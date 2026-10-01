#include "PlaygroundBehaviours.h"

#include <Illumo/Services/InputManager.h>
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>

void
registerPlaygroundBehaviours(BehaviourRegistry& registry)
{
  registry.add<SpinnerBehaviour>("playground.spinner");
  registry.add<BobBehaviour>("playground.bob");
  registry.add<OrbitBehaviour>("playground.orbit");
  registry.add<WalkerBehaviour>("playground.walker");
}

static constexpr double kTwoPi = 6.283185307179586;

// The node's parent's world matrix (identity for a root).
static Matrix4
parentWorld(const SceneBehaviourContext& context)
{
  const std::string parent = context.scene().parentOf(context.nodeId());
  return parent.empty() ? Matrix4(1.0f) : context.scene().worldMatrix(parent);
}

void
SpinnerBehaviour::update(SceneBehaviourContext& context, double elapsed)
{
  const SceneNode* node = context.node();
  Vector3 axis = context.values().vector("axis", Vector3(0.0f, 1.0f, 0.0f));
  if (node == nullptr || glm::length(axis) < 1.0e-6f) {
    return;
  }
  axis = glm::normalize(axis);
  const float angle = static_cast<float>(
    glm::radians(context.values().number("speed", 90.0) * elapsed));
  Transform3D transform = node->transform;
  transform.rotation =
    glm::normalize(glm::angleAxis(angle, axis) * transform.rotation);
  context.setTransform(transform);
}

void
BobBehaviour::start(SceneBehaviourContext& context)
{
  if (context.node() != nullptr) {
    m_base = context.node()->transform.position;
  }
}

void
BobBehaviour::update(SceneBehaviourContext& context, double elapsed)
{
  (void)elapsed;
  const SceneNode* node = context.node();
  if (node == nullptr) {
    return;
  }
  const double period = context.values().number("period", 2.0);
  const double height = context.values().number("height", 0.5);
  Transform3D transform = node->transform;
  transform.position = m_base;
  transform.position.y += static_cast<float>(
    height * std::sin(kTwoPi * context.time() / std::max(0.05, period)));
  context.setTransform(transform);
}

void
BobBehaviour::stop(SceneBehaviourContext& context)
{
  // Leave the node where it started.
  const SceneNode* node = context.node();
  if (node != nullptr) {
    Transform3D transform = node->transform;
    transform.position = m_base;
    context.setTransform(transform);
  }
}

void
OrbitBehaviour::start(SceneBehaviourContext& context)
{
  // Start from where the node already is around its target.
  const std::string& target = context.values().text("target");
  if (context.node() == nullptr || target.empty() ||
      context.scene().findNode(target) == nullptr) {
    return;
  }
  const Vector3 centre(context.scene().worldMatrix(target)[3]);
  const Vector3 here(context.scene().worldMatrix(context.nodeId())[3]);
  m_angle = std::atan2(here.z - centre.z, here.x - centre.x);
}

void
OrbitBehaviour::update(SceneBehaviourContext& context, double elapsed)
{
  const std::string& target = context.values().text("target");
  const SceneNode* node = context.node();
  if (node == nullptr || target.empty() ||
      context.scene().findNode(target) == nullptr) {
    return;
  }
  m_angle += glm::radians(context.values().number("speed", 45.0) * elapsed);
  const double radius = context.values().number("radius", 2.0);
  const Vector3 centre(context.scene().worldMatrix(target)[3]);
  const Vector3 world(centre.x + static_cast<float>(radius * std::cos(m_angle)),
                      centre.y,
                      centre.z +
                        static_cast<float>(radius * std::sin(m_angle)));
  Transform3D transform = node->transform;
  transform.position =
    Vector3(glm::inverse(parentWorld(context)) * glm::vec4(world, 1.0f));
  context.setTransform(transform);
}

void
WalkerBehaviour::update(SceneBehaviourContext& context, double elapsed)
{
  InputManager* input = context.input();
  const SceneNode* node = context.node();
  if (input == nullptr || node == nullptr) {
    return;
  }
  float right = 0.0f;
  float forward = 0.0f;
  if (input->isKeyPressed(KeyCode::Left) || input->isKeyPressed(KeyCode::A)) {
    right -= 1.0f;
  }
  if (input->isKeyPressed(KeyCode::Right) || input->isKeyPressed(KeyCode::D)) {
    right += 1.0f;
  }
  if (input->isKeyPressed(KeyCode::Up) || input->isKeyPressed(KeyCode::W)) {
    forward += 1.0f;
  }
  if (input->isKeyPressed(KeyCode::Down) || input->isKeyPressed(KeyCode::S)) {
    forward -= 1.0f;
  }
  if (right == 0.0f && forward == 0.0f) {
    return;
  }
  const float step =
    static_cast<float>(context.values().number("speed", 3.0) * elapsed);
  Transform3D transform = node->transform;
  transform.position.x += right * step;
  if (context.scene().document().worldMode == SceneWorldMode::World3D) {
    // Forward is away from a camera looking down -Z.
    transform.position.z -= forward * step;
  } else {
    transform.position.y += forward * step;
  }
  context.setTransform(transform);
}
