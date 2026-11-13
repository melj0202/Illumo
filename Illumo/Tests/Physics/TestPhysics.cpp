// Consumers of Illumo::Physics must never see vendored Jolt headers.
#if __has_include(<Jolt/Jolt.h>) || __has_include(<jolt-5.6.0/Jolt/Jolt.h>)
#error "Illumo::Physics leaks vendored Jolt headers to public consumers"
#endif

#include <Illumo/Physics/PhysicsWorld.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <algorithm>
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

static void
testCharacterSlopesLedgesAndCeiling()
{
  testSection(
    "Physics: slope standstill, ledge overhang drop and ceiling clamp");
  PhysicsWorld world;
  std::string error;
  testTrue(g, world.initialize(error), "world initializes");

  PhysicsBodyDesc groundDesc;
  groundDesc.shape = PhysicsShapeDesc::plane();
  groundDesc.motion = PhysicsMotionKind::Static;
  groundDesc.position = Vector3(0.0f, 0.06f, 0.0f);
  testTrue(g, world.createBody(groundDesc, error).isValid(), "ground created");

  // 1. Walkable 22-degree slope standstill.
  PhysicsBodyDesc rampDesc;
  rampDesc.shape = PhysicsShapeDesc::box(Vector3(1.0f, 0.06f, 1.0f));
  rampDesc.motion = PhysicsMotionKind::Static;
  rampDesc.position = Vector3(-4.0f, 0.50f, 0.0f);
  rampDesc.rotation = glm::angleAxis(0.3839724f, Vector3(1.0f, 0.0f, 0.0f));
  testTrue(g, world.createBody(rampDesc, error).isValid(), "ramp created");

  PhysicsCharacterDesc playerDesc;
  playerDesc.radius = 0.125f;
  playerDesc.height = 0.5f;
  playerDesc.position = Vector3(-4.0f, 1.00f, 0.0f);
  playerDesc.stepUpHeight = 0.15f;
  playerDesc.stickToFloorStepDown = 0.15f;
  const PhysicsCharacterHandle player =
    world.createCharacter(playerDesc, error);
  testTrue(g, player.isValid(), "character created");

  const float dt = 1.0f / 60.0f;
  for (int i = 0; i < 45; ++i) {
    const float vy =
      world.isCharacterGrounded(player)
        ? 0.0f
        : world.getCharacterLinearVelocity(player).y + world.gravity().y * dt;
    world.setCharacterLinearVelocity(player, Vector3(0.0f, vy, 0.0f));
    world.step(dt);
  }
  testTrue(
    g, world.isCharacterGrounded(player), "lands grounded on 22 deg slope");

  const Vector3 slopeStart = world.getCharacterPosition(player);
  testTrue(g,
           std::fabs(slopeStart.x - (-4.0f)) < 0.002f &&
             std::fabs(slopeStart.z) < 0.002f,
           "capsule drop onto 22 deg slope has no downhill XZ drift");
  for (int i = 0; i < 120; ++i) {
    const float vy =
      world.isCharacterGrounded(player)
        ? 0.0f
        : world.getCharacterLinearVelocity(player).y + world.gravity().y * dt;
    world.setCharacterLinearVelocity(player, Vector3(0.0f, vy, 0.0f));
    world.step(dt);
  }
  const Vector3 slopeEnd = world.getCharacterPosition(player);
  testTrue(g,
           world.isCharacterGrounded(player) &&
             glm::length(slopeEnd - slopeStart) < 0.001f,
           "idle character does not slide down walkable slope");

  float capsuleJumpPeakY = slopeEnd.y;
  for (int jump = 0; jump < 3; ++jump) {
    testTrue(g,
             world.isCharacterGrounded(player),
             "capsule is grounded before ramp jump");
    world.setCharacterLinearVelocity(player, Vector3(0.0f, 4.5f, 0.0f));
    world.step(dt);
    for (int i = 0; i < 60; ++i) {
      capsuleJumpPeakY =
        std::max(capsuleJumpPeakY, world.getCharacterPosition(player).y);
      const float vy =
        world.isCharacterGrounded(player)
          ? 0.0f
          : world.getCharacterLinearVelocity(player).y + world.gravity().y * dt;
      world.setCharacterLinearVelocity(player, Vector3(0.0f, vy, 0.0f));
      world.step(dt);
    }
  }
  const Vector3 capsuleAfterJump = world.getCharacterPosition(player);
  testTrue(g,
           world.isCharacterGrounded(player) &&
             capsuleJumpPeakY > slopeEnd.y + 0.20f &&
             std::hypot(capsuleAfterJump.x - slopeEnd.x,
                        capsuleAfterJump.z - slopeEnd.z) < 0.002f,
           "jumping in place on 22 deg slope produces no capsule XZ drift");

  // 2. Overhanging box corner drops promptly under gravity instead of creeping.
  PhysicsBodyDesc boxDesc;
  boxDesc.shape = PhysicsShapeDesc::box(Vector3(0.18f, 0.18f, 0.18f));
  boxDesc.motion = PhysicsMotionKind::Static;
  boxDesc.position = Vector3(1.25f, 0.24f, 0.0f);
  testTrue(
    g, world.createBody(boxDesc, error).isValid(), "elevated box created");

  world.setCharacterPosition(player, Vector3(1.03f, 0.64f, 0.0f));
  world.setCharacterLinearVelocity(player, Vector3(0.0f, 0.0f, 0.0f));
  testTrue(g,
           !world.isCharacterGrounded(player) &&
             world.getCharacterGroundState(player) ==
               PhysicsCharacterGroundState::NotSupported,
           "overhanging corner contact is not falsely reported as grounded");

  int fallTicks = 0;
  for (int i = 0; i < 30; ++i) {
    const float vy =
      world.isCharacterGrounded(player)
        ? 0.0f
        : world.getCharacterLinearVelocity(player).y + world.gravity().y * dt;
    world.setCharacterLinearVelocity(player, Vector3(0.0f, vy, 0.0f));
    world.step(dt);
    ++fallTicks;
    if (world.isCharacterGrounded(player) &&
        world.getCharacterPosition(player).y <= 0.33f) {
      break;
    }
  }
  testTrue(g,
           world.isCharacterGrounded(player) &&
             world.getCharacterPosition(player).y <= 0.33f && fallTicks <= 25,
           "overhanging corner falls promptly onto ground plane");

  // 3. Ceiling head-bonk zeroes residual upward velocity.
  world.setCharacterPosition(player, Vector3(3.0f, 0.31f, 0.0f));
  PhysicsBodyDesc ceilingDesc;
  ceilingDesc.shape = PhysicsShapeDesc::box(Vector3(1.0f, 0.1f, 1.0f));
  ceilingDesc.motion = PhysicsMotionKind::Static;
  // Character top at y = 0.56; ceiling bottom at y = 0.68 -> 0.12 m clearance.
  ceilingDesc.position = Vector3(3.0f, 0.78f, 0.0f);
  testTrue(
    g, world.createBody(ceilingDesc, error).isValid(), "ceiling created");

  world.setCharacterLinearVelocity(player, Vector3(0.0f, 6.2f, 0.0f));
  for (int i = 0; i < 3; ++i) {
    world.step(dt);
    if (i < 2) {
      const float vy =
        world.getCharacterLinearVelocity(player).y + world.gravity().y * dt;
      world.setCharacterLinearVelocity(player, Vector3(0.0f, vy, 0.0f));
    }
  }
  testTrue(g,
           world.getCharacterLinearVelocity(player).y <= 0.01f,
           "ceiling impact clears upward jump velocity immediately");

  // 4. Flat-bottom cylinder character: toe-on-ledge standing vs outer graze
  // drop.
  PhysicsCharacterDesc cylDesc;
  cylDesc.shape = PhysicsCharacterShape::Cylinder;
  cylDesc.radius = 0.125f;
  cylDesc.height = 0.5f;
  cylDesc.position = Vector3(1.01f, 0.67f, 0.0f);
  cylDesc.stepUpHeight = 0.15f;
  cylDesc.stickToFloorStepDown = 0.15f;
  const PhysicsCharacterHandle cyl = world.createCharacter(cylDesc, error);
  testTrue(g, cyl.isValid(), "cylinder character created");

  for (int i = 0; i < 60; ++i) {
    const float vy =
      world.isCharacterGrounded(cyl)
        ? 0.0f
        : world.getCharacterLinearVelocity(cyl).y + world.gravity().y * dt;
    world.setCharacterLinearVelocity(cyl, Vector3(0.0f, vy, 0.0f));
    world.step(dt);
  }
  testTrue(g,
           world.isCharacterGrounded(cyl) &&
             nearValue(world.getCharacterPosition(cyl).y, 0.67f, 0.02f),
           "cylinder character stands flat-footed on partial ledge overlap");

  // Outside visual edge [1.07, 1.43] (right edge at 0.93 + 0.125 = 1.055):
  // padding graze does not support or slant; cylinder falls onto ground plane.
  world.setCharacterPosition(cyl, Vector3(0.93f, 0.65f, 0.0f));
  world.setCharacterLinearVelocity(cyl, Vector3(0.0f, 0.0f, 0.0f));
  testTrue(g,
           !world.isCharacterGrounded(cyl),
           "cylinder outside ledge edge is not falsely grounded");

  int cylFallTicks = 0;
  for (int i = 0; i < 30; ++i) {
    const float vy =
      world.isCharacterGrounded(cyl)
        ? 0.0f
        : world.getCharacterLinearVelocity(cyl).y + world.gravity().y * dt;
    world.setCharacterLinearVelocity(cyl, Vector3(0.0f, vy, 0.0f));
    world.step(dt);
    ++cylFallTicks;
    if (world.isCharacterGrounded(cyl) &&
        world.getCharacterPosition(cyl).y <= 0.33f) {
      break;
    }
  }
  testTrue(g,
           world.isCharacterGrounded(cyl) &&
             world.getCharacterPosition(cyl).y <= 0.33f && cylFallTicks <= 25,
           "cylinder outside ledge edge drops straight onto ground plane");

  // 5. Flat-bottom cylinder character: drop & repeated vertical jumps on 22 deg
  // slope produce no downhill XZ slide.
  world.setCharacterPosition(cyl, Vector3(-4.0f, 1.00f, 0.0f));
  world.setCharacterLinearVelocity(cyl, Vector3(0.0f, 0.0f, 0.0f));
  for (int i = 0; i < 45; ++i) {
    const float vy =
      world.isCharacterGrounded(cyl)
        ? 0.0f
        : world.getCharacterLinearVelocity(cyl).y + world.gravity().y * dt;
    world.setCharacterLinearVelocity(cyl, Vector3(0.0f, vy, 0.0f));
    world.step(dt);
  }
  const Vector3 cylSlopeStart = world.getCharacterPosition(cyl);
  testTrue(g,
           world.isCharacterGrounded(cyl) &&
             std::hypot(cylSlopeStart.x - (-4.0f), cylSlopeStart.z) < 0.002f,
           "cylinder drop onto 22 deg slope has no downhill XZ drift");

  float cylJumpPeakY = cylSlopeStart.y;
  for (int jump = 0; jump < 3; ++jump) {
    testTrue(g,
             world.isCharacterGrounded(cyl),
             "cylinder is grounded before ramp jump");
    world.setCharacterLinearVelocity(cyl, Vector3(0.0f, 4.5f, 0.0f));
    world.step(dt);
    for (int i = 0; i < 60; ++i) {
      cylJumpPeakY = std::max(cylJumpPeakY, world.getCharacterPosition(cyl).y);
      const float vy =
        world.isCharacterGrounded(cyl)
          ? 0.0f
          : world.getCharacterLinearVelocity(cyl).y + world.gravity().y * dt;
      world.setCharacterLinearVelocity(cyl, Vector3(0.0f, vy, 0.0f));
      world.step(dt);
    }
  }
  const Vector3 cylAfterJump = world.getCharacterPosition(cyl);
  testTrue(g,
           world.isCharacterGrounded(cyl) &&
             cylJumpPeakY > cylSlopeStart.y + 0.20f &&
             std::hypot(cylAfterJump.x - cylSlopeStart.x,
                        cylAfterJump.z - cylSlopeStart.z) < 0.002f,
           "jumping in place on 22 deg slope produces no cylinder XZ drift");
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
  registry.add("Illumo.Physics.CharacterSlopesLedgesAndCeiling", []() {
    g = {};
    testCharacterSlopesLedgesAndCeiling();
    return g.failures;
  });
}
