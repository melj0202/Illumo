#pragma once

#include <Illumo/Foundation/MathTypes.h>
#include <cstddef>
#include <cstdint>

struct PhysicsBodyHandle
{
  uint32_t slot = 0;
  uint32_t generation = 0;

  constexpr bool isValid() const noexcept { return generation != 0; }
  constexpr bool operator==(const PhysicsBodyHandle&) const noexcept = default;
};

struct PhysicsCharacterHandle
{
  uint32_t slot = 0;
  uint32_t generation = 0;

  constexpr bool isValid() const noexcept { return generation != 0; }
  constexpr bool operator==(const PhysicsCharacterHandle&) const noexcept =
    default;
};

enum class PhysicsShapeKind : uint8_t
{
  Plane = 0,
  Box = 1,
  Sphere = 2,
  Capsule = 3
};

enum class PhysicsMotionKind : uint8_t
{
  Static = 0,
  Kinematic = 1,
  Dynamic = 2
};

enum class PhysicsCharacterGroundState : uint8_t
{
  Grounded = 0,
  OnSteepGround = 1,
  NotSupported = 2,
  InAir = 3
};

struct PhysicsShapeDesc
{
  PhysicsShapeKind kind = PhysicsShapeKind::Box;
  Vector3 halfExtents{ 0.5f, 0.5f, 0.5f };
  Vector3 normal{ 0.0f, 1.0f, 0.0f };
  float planeHalfExtent = 1000.0f;
  float radius = 0.5f;
  // Total capsule height along local +Y, including both hemispherical caps.
  float height = 1.0f;
  Vector3 offset{ 0.0f, 0.0f, 0.0f };
  Quaternion rotationOffset{ 1.0f, 0.0f, 0.0f, 0.0f };

  static PhysicsShapeDesc plane(const Vector3& planeNormal = Vector3(0.0f,
                                                                     1.0f,
                                                                     0.0f),
                                float halfExtent = 1000.0f)
  {
    PhysicsShapeDesc desc;
    desc.kind = PhysicsShapeKind::Plane;
    desc.normal = planeNormal;
    desc.planeHalfExtent = halfExtent;
    return desc;
  }

  static PhysicsShapeDesc box(const Vector3& extents)
  {
    PhysicsShapeDesc desc;
    desc.kind = PhysicsShapeKind::Box;
    desc.halfExtents = extents;
    return desc;
  }

  static PhysicsShapeDesc sphere(float sphereRadius)
  {
    PhysicsShapeDesc desc;
    desc.kind = PhysicsShapeKind::Sphere;
    desc.radius = sphereRadius;
    return desc;
  }

  static PhysicsShapeDesc capsule(float capsuleRadius, float totalHeight)
  {
    PhysicsShapeDesc desc;
    desc.kind = PhysicsShapeKind::Capsule;
    desc.radius = capsuleRadius;
    desc.height = totalHeight;
    return desc;
  }
};

struct PhysicsBodyDesc
{
  PhysicsShapeDesc shape{};
  PhysicsMotionKind motion = PhysicsMotionKind::Static;
  Vector3 position{ 0.0f, 0.0f, 0.0f };
  Quaternion rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
  float mass = 1.0f;
  float friction = 0.5f;
  float restitution = 0.0f;
  float linearDamping = 0.05f;
  float angularDamping = 0.05f;
  float gravityFactor = 1.0f;
  bool isSensor = false;
  bool lockRotationX = false;
  bool lockRotationY = false;
  bool lockRotationZ = false;
  uint64_t userData = 0;
};

struct PhysicsCharacterDesc
{
  float radius = 0.125f;
  // Total capsule height along +Y, including both hemispherical caps.
  // The character's position is the geometric center of this capsule.
  float height = 0.5f;
  Vector3 position{ 0.0f, 0.0f, 0.0f };
  Quaternion rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
  float mass = 70.0f;
  float maxStrength = 100.0f;
  float maxSlopeDegrees = 50.0f;
  float stepUpHeight = 0.15f;
  float stickToFloorStepDown = 0.15f;
  float characterPadding = 0.02f;
  uint64_t userData = 0;
};

struct PhysicsRayHit
{
  PhysicsBodyHandle body{};
  PhysicsCharacterHandle character{};
  Vector3 point{ 0.0f, 0.0f, 0.0f };
  Vector3 normal{ 0.0f, 1.0f, 0.0f };
  float fraction = 1.0f;
  float distance = 0.0f;
  uint64_t userData = 0;
};

struct PhysicsOverlapPair
{
  PhysicsBodyHandle sensorBody{};
  PhysicsBodyHandle otherBody{};
  PhysicsCharacterHandle otherCharacter{};
  uint64_t sensorUserData = 0;
  uint64_t otherUserData = 0;
};

struct PhysicsWorldConfig
{
  uint32_t maxBodies = 2048;
  uint32_t maxBodyPairs = 4096;
  uint32_t maxContactConstraints = 2048;
  size_t tempArenaBytes = 4u * 1024u * 1024u;
  Vector3 gravity{ 0.0f, -18.0f, 0.0f };
  float penetrationSlop = 0.002f;
};
