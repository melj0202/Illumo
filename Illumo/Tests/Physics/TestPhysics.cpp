// Consumers of Illumo::Physics must never see vendored Jolt headers.
#if __has_include(<Jolt/Jolt.h>) || __has_include(<jolt-5.6.0/Jolt/Jolt.h>)
#error "Illumo::Physics leaks vendored Jolt headers to public consumers"
#endif

#include <Illumo/Physics/PhysicsWorld.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cmath>
#include <string>

static TestCounters g;

static bool
nearValue(float actual, float expected, float tolerance = 0.015f)
{
  return std::fabs(actual - expected) <= tolerance;
}

static void
testLifecycleAndHandles()
{
  testSection("Physics: world lifecycle and generational handles");
  PhysicsWorld worldA;
  PhysicsWorld worldB;
  std::string error;

  testTrue(g, !worldA.isInitialized(), "starts uninitialized");
  testTrue(g, worldA.initialize(error), "worldA initializes");
  testTrue(g, worldB.initialize(error), "worldB initializes concurrently");
  testTrue(g, worldA.isInitialized(), "worldA reports initialized");
  testTrue(g, worldB.isInitialized(), "worldB reports initialized");

  PhysicsBodyDesc dynamicPlane;
  dynamicPlane.shape = PhysicsShapeDesc::plane();
  dynamicPlane.motion = PhysicsMotionKind::Dynamic;
  const PhysicsBodyHandle invalidPlane = worldA.createBody(dynamicPlane, error);
  testTrue(g, !invalidPlane.isValid(), "dynamic plane rejected");
  testTrue(g, !error.empty(), "dynamic plane reports error");

  error.clear();
  PhysicsBodyDesc boxDesc;
  boxDesc.shape = PhysicsShapeDesc::box(Vector3(0.5f, 0.5f, 0.5f));
  boxDesc.motion = PhysicsMotionKind::Dynamic;
  boxDesc.position = Vector3(1.0f, 2.0f, 3.0f);
  const PhysicsBodyHandle first = worldA.createBody(boxDesc, error);
  testTrue(g, first.isValid(), "body handle valid");
  testTrue(g, worldA.isBodyValid(first), "worldA recognizes body");
  testTrue(g, !worldB.isBodyValid(first), "worldB does not confuse slot");

  worldA.destroyBody(first);
  testTrue(g, !worldA.isBodyValid(first), "destroyed body handle invalid");

  const PhysicsBodyHandle reused = worldA.createBody(boxDesc, error);
  testTrue(g, reused.isValid(), "reused slot valid");
  testTrue(g,
           reused.slot == first.slot && reused.generation != first.generation,
           "slot reused with bumped generation");
  testTrue(g, !worldA.isBodyValid(first), "stale body handle rejected");
  testTrue(g, worldA.isBodyValid(reused), "fresh generation accepted");

  PhysicsCharacterDesc charDesc;
  const PhysicsCharacterHandle firstChar =
    worldA.createCharacter(charDesc, error);
  testTrue(g, firstChar.isValid(), "character handle valid");
  testTrue(g, worldA.isCharacterValid(firstChar), "character recognized");
  worldA.destroyCharacter(firstChar);
  testTrue(g, !worldA.isCharacterValid(firstChar), "destroyed char invalid");

  const PhysicsCharacterHandle secondChar =
    worldA.createCharacter(charDesc, error);
  testTrue(g,
           secondChar.isValid() && secondChar.slot == firstChar.slot &&
             secondChar.generation != firstChar.generation,
           "character slot reused with bumped generation");
  testTrue(g, !worldA.isCharacterValid(firstChar), "stale char rejected");

  worldA.shutdown();
  testTrue(g, !worldA.isInitialized(), "worldA shuts down");
  testTrue(g, worldB.isInitialized(), "worldB stays alive");
  worldB.step(1.0f / 60.0f);
  worldB.shutdown();
}

static void
testRigidBodyFallAndRest()
{
  testSection("Physics: dynamic box falls under gravity and rests on plane");
  PhysicsWorld world;
  std::string error;
  testTrue(g, world.initialize(error), "world initializes");

  PhysicsBodyDesc groundDesc;
  groundDesc.shape = PhysicsShapeDesc::plane();
  groundDesc.motion = PhysicsMotionKind::Static;
  groundDesc.position = Vector3(0.0f, 0.06f, 0.0f);
  const PhysicsBodyHandle ground = world.createBody(groundDesc, error);
  testTrue(g, ground.isValid(), "ground plane created");

  PhysicsBodyDesc crateDesc;
  crateDesc.shape = PhysicsShapeDesc::box(Vector3(0.25f, 0.25f, 0.25f));
  crateDesc.motion = PhysicsMotionKind::Dynamic;
  crateDesc.position = Vector3(0.0f, 2.0f, 0.0f);
  crateDesc.restitution = 0.0f;
  crateDesc.lockRotationX = true;
  crateDesc.lockRotationY = true;
  crateDesc.lockRotationZ = true;
  const PhysicsBodyHandle crate = world.createBody(crateDesc, error);
  testTrue(g, crate.isValid(), "dynamic crate created");

  for (int i = 0; i < 120; ++i) {
    world.step(1.0f / 60.0f);
  }

  const Vector3 resting = world.getBodyPosition(crate);
  testTrue(g,
           nearValue(resting.x, 0.0f, 0.02f) &&
             nearValue(resting.y, 0.31f, 0.02f) &&
             nearValue(resting.z, 0.0f, 0.02f),
           "crate settles cleanly on analytical plane at y = 0.31");
}

static void
testKinematicRaycastAndSensor()
{
  testSection("Physics: kinematic motion, raycast and sensor overlap");
  PhysicsWorld world;
  std::string error;
  testTrue(g, world.initialize(error), "world initializes");

  PhysicsBodyDesc wallDesc;
  wallDesc.shape = PhysicsShapeDesc::box(Vector3(1.0f, 1.0f, 0.25f));
  wallDesc.motion = PhysicsMotionKind::Static;
  wallDesc.position = Vector3(0.0f, 1.0f, -3.0f);
  wallDesc.userData = 42;
  const PhysicsBodyHandle wall = world.createBody(wallDesc, error);
  testTrue(g, wall.isValid(), "wall created");

  PhysicsBodyDesc sensorDesc;
  sensorDesc.shape = PhysicsShapeDesc::box(Vector3(0.5f, 0.5f, 0.5f));
  sensorDesc.motion = PhysicsMotionKind::Static;
  sensorDesc.position = Vector3(0.0f, 1.0f, -1.5f);
  sensorDesc.isSensor = true;
  sensorDesc.userData = 99;
  const PhysicsBodyHandle sensor = world.createBody(sensorDesc, error);
  testTrue(g, sensor.isValid(), "sensor trigger created");

  PhysicsRayHit solidHit;
  testTrue(g,
           world.raycast(Vector3(0.0f, 1.0f, 0.0f),
                         Vector3(0.0f, 0.0f, -1.0f),
                         10.0f,
                         solidHit,
                         false),
           "solid raycast hits wall");
  testTrue(g,
           solidHit.body == wall && solidHit.userData == 42 &&
             nearValue(solidHit.point.z, -2.75f, 0.02f) &&
             nearValue(solidHit.normal.z, 1.0f, 0.02f),
           "solid raycast skips sensor and reports wall normal and point");

  PhysicsRayHit sensorHit;
  testTrue(g,
           world.raycast(Vector3(0.0f, 1.0f, 0.0f),
                         Vector3(0.0f, 0.0f, -1.0f),
                         10.0f,
                         sensorHit,
                         true),
           "sensor-inclusive raycast hits trigger");
  testTrue(g,
           sensorHit.body == sensor && sensorHit.userData == 99,
           "sensor raycast reports trigger handle and userData");

  PhysicsBodyDesc moverDesc;
  moverDesc.shape = PhysicsShapeDesc::sphere(0.25f);
  moverDesc.motion = PhysicsMotionKind::Kinematic;
  moverDesc.position = Vector3(-2.0f, 1.0f, -1.5f);
  moverDesc.userData = 7;
  const PhysicsBodyHandle mover = world.createBody(moverDesc, error);
  testTrue(g, mover.isValid(), "kinematic sphere created");

  world.step(1.0f / 60.0f);
  testTrue(g, world.sensorOverlaps().empty(), "no initial sensor overlap");

  const float dt = 1.0f / 60.0f;
  world.moveKinematicBody(
    mover, Vector3(0.0f, 1.0f, -1.5f), Quaternion(1.0f, 0.0f, 0.0f, 0.0f), dt);
  world.step(dt);
  world.step(dt);

  const std::span<const PhysicsOverlapPair> overlaps = world.sensorOverlaps();
  testTrue(g, overlaps.size() == 1, "sensor reports active overlap");
  if (overlaps.size() == 1) {
    testTrue(
      g,
      overlaps[0].sensorBody == sensor && overlaps[0].otherBody == mover &&
        overlaps[0].sensorUserData == 99 && overlaps[0].otherUserData == 7,
      "sensor overlap identifies both bodies and payloads");
  }

  world.moveKinematicBody(
    mover, Vector3(2.0f, 1.0f, -1.5f), Quaternion(1.0f, 0.0f, 0.0f, 0.0f), dt);
  world.step(dt);
  world.step(dt);
  testTrue(g, world.sensorOverlaps().empty(), "sensor overlap clears on exit");
}

static void
testCharacterGroundSlideStairsAndPush()
{
  testSection("Physics: character ground support, slide, step-up and push");
  PhysicsWorld world;
  std::string error;
  testTrue(g, world.initialize(error), "world initializes");

  PhysicsBodyDesc groundDesc;
  groundDesc.shape = PhysicsShapeDesc::plane();
  groundDesc.motion = PhysicsMotionKind::Static;
  groundDesc.position = Vector3(0.0f, 0.06f, 0.0f);
  testTrue(g, world.createBody(groundDesc, error).isValid(), "ground created");

  // Spawn standing directly at Saturn's exact player pose (y = 0.31 on y =
  // 0.06).
  PhysicsCharacterDesc playerDesc;
  playerDesc.radius = 0.125f;
  playerDesc.height = 0.5f;
  playerDesc.position = Vector3(0.0f, 0.31f, 0.0f);
  playerDesc.stepUpHeight = 0.15f;
  playerDesc.stickToFloorStepDown = 0.15f;
  playerDesc.userData = 1001;
  const PhysicsCharacterHandle player =
    world.createCharacter(playerDesc, error);
  testTrue(g, player.isValid(), "character created");
  testTrue(g, world.isCharacterGrounded(player), "character starts grounded");
  testTrue(g,
           nearValue(world.getCharacterPosition(player).y, 0.31f, 0.01f),
           "character rests at y = 0.31");

  // Diagonal wall rotated 45 degrees around +Y in front of +X movement:
  // character moving +X should slide cleanly in Z rather than snagging.
  PhysicsBodyDesc wallDesc;
  wallDesc.shape = PhysicsShapeDesc::box(Vector3(0.15f, 0.5f, 2.0f));
  wallDesc.motion = PhysicsMotionKind::Static;
  wallDesc.position = Vector3(1.0f, 0.56f, 0.0f);
  wallDesc.rotation = glm::angleAxis(0.78539816f, Vector3(0.0f, 1.0f, 0.0f));
  const PhysicsBodyHandle wall = world.createBody(wallDesc, error);
  testTrue(g, wall.isValid(), "diagonal wall created");

  const float dt = 1.0f / 60.0f;
  for (int i = 0; i < 60; ++i) {
    world.setCharacterLinearVelocity(player, Vector3(3.0f, -0.3f, 0.0f));
    world.step(dt);
  }
  const Vector3 slid = world.getCharacterPosition(player);
  testTrue(g,
           std::fabs(slid.z) > 0.4f && nearValue(slid.y, 0.31f, 0.02f),
           "character slides laterally along angled wall");
  world.destroyBody(wall);

  // Low step (0.10 m high, top at y = 0.16 -> resting center y = 0.41).
  world.setCharacterPosition(player, Vector3(0.0f, 0.31f, 3.0f));
  PhysicsBodyDesc stepDesc;
  stepDesc.shape = PhysicsShapeDesc::box(Vector3(0.6f, 0.05f, 0.6f));
  stepDesc.motion = PhysicsMotionKind::Static;
  stepDesc.position = Vector3(0.8f, 0.11f, 3.0f);
  const PhysicsBodyHandle stair = world.createBody(stepDesc, error);
  testTrue(g, stair.isValid(), "low step created");

  for (int i = 0; i < 30; ++i) {
    world.setCharacterLinearVelocity(player, Vector3(3.0f, -0.3f, 0.0f));
    world.step(dt);
  }
  const Vector3 stepped = world.getCharacterPosition(player);
  testTrue(g,
           stepped.x > 0.5f && nearValue(stepped.y, 0.41f, 0.025f),
           "character steps onto low stair");
  world.destroyBody(stair);

  // Dynamic crate push along +X.
  world.setCharacterPosition(player, Vector3(0.0f, 0.31f, -3.0f));
  PhysicsBodyDesc crateDesc;
  crateDesc.shape = PhysicsShapeDesc::box(Vector3(0.2f, 0.2f, 0.2f));
  crateDesc.motion = PhysicsMotionKind::Dynamic;
  crateDesc.position = Vector3(0.5f, 0.26f, -3.0f);
  crateDesc.mass = 5.0f;
  crateDesc.friction = 0.2f;
  crateDesc.lockRotationX = true;
  crateDesc.lockRotationY = true;
  crateDesc.lockRotationZ = true;
  const PhysicsBodyHandle crate = world.createBody(crateDesc, error);
  testTrue(g, crate.isValid(), "dynamic crate created");

  for (int i = 0; i < 60; ++i) {
    world.setCharacterLinearVelocity(player, Vector3(2.5f, -0.3f, 0.0f));
    world.step(dt);
  }
  testTrue(g, world.getBodyPosition(crate).x > 0.9f, "character pushes crate");
}

void
registerPhysicsTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Physics.LifecycleAndHandles", []() {
    g = {};
    testLifecycleAndHandles();
    return g.failures;
  });
  registry.add("Illumo.Physics.RigidBodyFallAndRest", []() {
    g = {};
    testRigidBodyFallAndRest();
    return g.failures;
  });
  registry.add("Illumo.Physics.KinematicRaycastAndSensor", []() {
    g = {};
    testKinematicRaycastAndSensor();
    return g.failures;
  });
  registry.add("Illumo.Physics.CharacterGroundSlideStairsAndPush", []() {
    g = {};
    testCharacterGroundSlideStairsAndPush();
    return g.failures;
  });
}
