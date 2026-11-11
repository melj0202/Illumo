#pragma once

#include <Illumo/Physics/PhysicsTypes.h>
#include <Illumo/Scene/Transform3D.h>
#include <memory>
#include <span>
#include <string>

class PhysicsWorld
{
public:
  PhysicsWorld();
  ~PhysicsWorld();

  PhysicsWorld(const PhysicsWorld&) = delete;
  PhysicsWorld& operator=(const PhysicsWorld&) = delete;
  PhysicsWorld(PhysicsWorld&&) = delete;
  PhysicsWorld& operator=(PhysicsWorld&&) = delete;

  bool initialize(const PhysicsWorldConfig& config, std::string& error);
  bool initialize(std::string& error);
  void shutdown() noexcept;
  bool isInitialized() const noexcept;

  void setGravity(const Vector3& gravity) noexcept;
  Vector3 gravity() const noexcept;

  PhysicsBodyHandle createBody(const PhysicsBodyDesc& desc, std::string& error);
  void destroyBody(PhysicsBodyHandle handle) noexcept;
  bool isBodyValid(PhysicsBodyHandle handle) const noexcept;

  Vector3 getBodyPosition(PhysicsBodyHandle handle) const noexcept;
  Quaternion getBodyRotation(PhysicsBodyHandle handle) const noexcept;
  Transform3D getBodyTransform(PhysicsBodyHandle handle) const noexcept;
  void setBodyPose(PhysicsBodyHandle handle,
                   const Vector3& position,
                   const Quaternion& rotation,
                   bool activate = true) noexcept;
  void moveKinematicBody(PhysicsBodyHandle handle,
                         const Vector3& targetPosition,
                         const Quaternion& targetRotation,
                         float deltaSeconds) noexcept;
  Vector3 getBodyLinearVelocity(PhysicsBodyHandle handle) const noexcept;
  void setBodyLinearVelocity(PhysicsBodyHandle handle,
                             const Vector3& velocity) noexcept;
  void addImpulse(PhysicsBodyHandle handle, const Vector3& impulse) noexcept;

  PhysicsCharacterHandle createCharacter(const PhysicsCharacterDesc& desc,
                                         std::string& error);
  void destroyCharacter(PhysicsCharacterHandle handle) noexcept;
  bool isCharacterValid(PhysicsCharacterHandle handle) const noexcept;

  Vector3 getCharacterPosition(PhysicsCharacterHandle handle) const noexcept;
  void setCharacterPosition(PhysicsCharacterHandle handle,
                            const Vector3& position) noexcept;
  Quaternion getCharacterRotation(PhysicsCharacterHandle handle) const noexcept;
  void setCharacterRotation(PhysicsCharacterHandle handle,
                            const Quaternion& rotation) noexcept;
  Vector3 getCharacterLinearVelocity(
    PhysicsCharacterHandle handle) const noexcept;
  void setCharacterLinearVelocity(PhysicsCharacterHandle handle,
                                  const Vector3& velocity) noexcept;
  Vector3 cancelVelocityTowardsSteepSlopes(
    PhysicsCharacterHandle handle,
    const Vector3& desiredVelocity) const noexcept;
  PhysicsCharacterGroundState getCharacterGroundState(
    PhysicsCharacterHandle handle) const noexcept;
  bool isCharacterGrounded(PhysicsCharacterHandle handle) const noexcept;
  void refreshCharacterContacts(PhysicsCharacterHandle handle) noexcept;
  Vector3 getCharacterGroundNormal(
    PhysicsCharacterHandle handle) const noexcept;
  Vector3 getCharacterGroundVelocity(
    PhysicsCharacterHandle handle) const noexcept;

  void step(float deltaSeconds, int collisionSteps = 1);

  bool raycast(const Vector3& origin,
               const Vector3& direction,
               float maxDistance,
               PhysicsRayHit& outHit,
               bool includeSensors = false) const;

  std::span<const PhysicsOverlapPair> sensorOverlaps() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
