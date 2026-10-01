#pragma once

#include <Illumo/Content/SceneBehaviours.h>

// The playground's sample behaviours, described in Playground/behaviours.json.
// They are small on purpose: each shows one way a behaviour reads its values,
// its node, other nodes or input. Copy them as a starting point.

// Registers every sample behaviour type.
void
registerPlaygroundBehaviours(BehaviourRegistry& registry);

// playground.spinner: turns its node about `axis` at `speed` degrees per
// second.
class SpinnerBehaviour final : public SceneBehaviour
{
public:
  void update(SceneBehaviourContext& context, double elapsed) override;
};

// playground.bob: moves its node up and down by `height` once every `period`
// seconds, around where it started.
class BobBehaviour final : public SceneBehaviour
{
public:
  void start(SceneBehaviourContext& context) override;
  void update(SceneBehaviourContext& context, double elapsed) override;
  void stop(SceneBehaviourContext& context) override;

private:
  Vector3 m_base{ 0.0f };
};

// playground.orbit: circles the `target` node at `radius`, `speed` degrees
// per second, at the target's height.
class OrbitBehaviour final : public SceneBehaviour
{
public:
  void start(SceneBehaviourContext& context) override;
  void update(SceneBehaviourContext& context, double elapsed) override;

private:
  double m_angle = 0.0;
};

// playground.walker: the arrow keys or WASD move its node across the ground
// plane at `speed` units per second (XZ in 3D, XY in 2D).
class WalkerBehaviour final : public SceneBehaviour
{
public:
  void update(SceneBehaviourContext& context, double elapsed) override;
};
