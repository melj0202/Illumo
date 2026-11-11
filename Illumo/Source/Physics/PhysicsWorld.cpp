#include <Illumo/Physics/PhysicsWorld.h>

// Jolt.h must precede all other Jolt headers.
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/IssueReporting.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/AllowedDOFs.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/PlaneShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#ifndef ILLUMO_SERIAL_GUEST
#include <mutex>
#endif
#include <vector>

static constexpr JPH::ObjectLayer kLayerNonMoving = 0;
static constexpr JPH::ObjectLayer kLayerMoving = 1;
static constexpr JPH::ObjectLayer kLayerSensor = 2;

static constexpr JPH::BroadPhaseLayer kBroadPhaseNonMoving(0);
static constexpr JPH::BroadPhaseLayer kBroadPhaseMoving(1);
static constexpr uint32_t kNumBroadPhaseLayers = 2;

static constexpr uint32_t kMaxPhysicsJobs = 256;
static constexpr float kMinExtent = 0.001f;
static constexpr float kEpsilonSq = 1.0e-8f;

static void
joltTraceNoOp(const char* format, ...)
{
  (void)format;
}

#ifdef ILLUMO_SERIAL_GUEST
// WASI guests execute single-threaded inside Illumo's host sandbox, which does
// not provide wasi_snapshot_preview1::sched_yield. Satisfy Jolt's spin-wait
// yield locally so libc's WASI import stub is never pulled into guest modules.
extern "C" int
sched_yield(void)
{
  return 0;
}
#else
static std::mutex g_joltInitMutex;
#endif
static uint32_t g_joltRefCount = 0;

static void
retainJoltGlobals()
{
#ifndef ILLUMO_SERIAL_GUEST
  std::scoped_lock<std::mutex> lock(g_joltInitMutex);
#endif
  if (g_joltRefCount == 0) {
    JPH::RegisterDefaultAllocator();
    JPH::Trace = joltTraceNoOp;
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
  }
  ++g_joltRefCount;
}

static void
releaseJoltGlobals() noexcept
{
#ifndef ILLUMO_SERIAL_GUEST
  std::scoped_lock<std::mutex> lock(g_joltInitMutex);
#endif
  if (g_joltRefCount == 0) {
    return;
  }
  --g_joltRefCount;
  if (g_joltRefCount == 0) {
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
  }
}

static JPH::Vec3
toJoltVec3(const Vector3& v) noexcept
{
  return JPH::Vec3(v.x, v.y, v.z);
}

static JPH::RVec3
toJoltRVec3(const Vector3& v) noexcept
{
  return JPH::RVec3(v.x, v.y, v.z);
}

static Vector3
fromJoltVec3(JPH::Vec3Arg v) noexcept
{
  return Vector3(v.GetX(), v.GetY(), v.GetZ());
}

static Vector3
fromJoltRVec3(JPH::RVec3Arg v) noexcept
{
  return Vector3(static_cast<float>(v.GetX()),
                 static_cast<float>(v.GetY()),
                 static_cast<float>(v.GetZ()));
}

static JPH::Quat
toJoltQuat(const Quaternion& q) noexcept
{
  const float lengthSq = q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z;
  if (lengthSq <= kEpsilonSq) {
    return JPH::Quat::sIdentity();
  }
  const Quaternion normalized = glm::normalize(q);
  return JPH::Quat(normalized.x, normalized.y, normalized.z, normalized.w);
}

static Quaternion
fromJoltQuat(JPH::QuatArg q) noexcept
{
  return Quaternion(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
}

class PhysicsBroadPhaseLayerMap final : public JPH::BroadPhaseLayerInterface
{
public:
  JPH::uint GetNumBroadPhaseLayers() const override
  {
    return kNumBroadPhaseLayers;
  }

  JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
  {
    if (layer == kLayerNonMoving) {
      return kBroadPhaseNonMoving;
    }
    return kBroadPhaseMoving;
  }
};

class PhysicsObjectVsBroadPhaseFilter final
  : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
  bool ShouldCollide(JPH::ObjectLayer objectLayer,
                     JPH::BroadPhaseLayer broadPhaseLayer) const override
  {
    if (objectLayer == kLayerNonMoving) {
      return broadPhaseLayer == kBroadPhaseMoving;
    }
    if (objectLayer == kLayerSensor) {
      return broadPhaseLayer == kBroadPhaseMoving;
    }
    return true;
  }
};

class PhysicsObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
{
public:
  bool ShouldCollide(JPH::ObjectLayer layerA,
                     JPH::ObjectLayer layerB) const override
  {
    if (layerA == kLayerSensor || layerB == kLayerSensor) {
      const JPH::ObjectLayer other = (layerA == kLayerSensor) ? layerB : layerA;
      return other == kLayerMoving;
    }
    if (layerA == kLayerNonMoving) {
      return layerB == kLayerMoving;
    }
    return true;
  }
};

class PhysicsSolidObjectLayerFilter final : public JPH::ObjectLayerFilter
{
public:
  bool ShouldCollide(JPH::ObjectLayer layer) const override
  {
    return layer == kLayerNonMoving || layer == kLayerMoving;
  }
};

class PhysicsAllObjectLayerFilter final : public JPH::ObjectLayerFilter
{
public:
  bool ShouldCollide(JPH::ObjectLayer layer) const override
  {
    (void)layer;
    return true;
  }
};

struct PhysicsActiveSensorSubShapePair
{
  JPH::SubShapeIDPair pair;
  JPH::BodyID sensorBodyId;
  JPH::BodyID otherBodyId;
};

class PhysicsSensorContactListener final : public JPH::ContactListener
{
public:
  explicit PhysicsSensorContactListener(size_t initialCapacity)
  {
    m_activeContacts.reserve(initialCapacity);
  }

  void clear() noexcept { m_activeContacts.clear(); }

  void removeBody(JPH::BodyID bodyId) noexcept
  {
    size_t i = 0;
    while (i < m_activeContacts.size()) {
      const PhysicsActiveSensorSubShapePair& entry = m_activeContacts[i];
      if (entry.sensorBodyId == bodyId || entry.otherBodyId == bodyId) {
        m_activeContacts[i] = m_activeContacts.back();
        m_activeContacts.pop_back();
      } else {
        ++i;
      }
    }
  }

  const std::vector<PhysicsActiveSensorSubShapePair>& activeContacts()
    const noexcept
  {
    return m_activeContacts;
  }

  void OnContactAdded(const JPH::Body& body1,
                      const JPH::Body& body2,
                      const JPH::ContactManifold& manifold,
                      JPH::ContactSettings& settings) override
  {
    (void)settings;
    const bool sensor1 = body1.IsSensor();
    const bool sensor2 = body2.IsSensor();
    if (sensor1 == sensor2) {
      return;
    }

    const JPH::SubShapeIDPair subPair(body1.GetID(),
                                      manifold.mSubShapeID1,
                                      body2.GetID(),
                                      manifold.mSubShapeID2);
    for (const PhysicsActiveSensorSubShapePair& existing : m_activeContacts) {
      if (existing.pair == subPair) {
        return;
      }
    }

    PhysicsActiveSensorSubShapePair entry;
    entry.pair = subPair;
    entry.sensorBodyId = sensor1 ? body1.GetID() : body2.GetID();
    entry.otherBodyId = sensor1 ? body2.GetID() : body1.GetID();
    m_activeContacts.push_back(entry);
  }

  void OnContactRemoved(const JPH::SubShapeIDPair& subShapePair) override
  {
    for (size_t i = 0; i < m_activeContacts.size(); ++i) {
      if (m_activeContacts[i].pair == subShapePair) {
        m_activeContacts[i] = m_activeContacts.back();
        m_activeContacts.pop_back();
        return;
      }
    }
  }

private:
  std::vector<PhysicsActiveSensorSubShapePair> m_activeContacts;
};

struct PhysicsBodyReverseEntry
{
  PhysicsBodyHandle body{};
  PhysicsCharacterHandle character{};
  uint64_t userData = 0;
  bool occupied = false;
};

struct PhysicsBodySlot
{
  JPH::BodyID bodyId{};
  uint32_t generation = 1;
  uint64_t userData = 0;
  bool alive = false;
};

struct PhysicsCharacterSlot
{
  JPH::Ref<JPH::CharacterVirtual> character{};
  JPH::CharacterVirtual::ExtendedUpdateSettings extendedSettings{};
  uint32_t generation = 1;
  uint64_t userData = 0;
  bool alive = false;
};

static JPH::RefConst<JPH::Shape>
buildJoltShape(const PhysicsShapeDesc& desc, std::string& error)
{
  JPH::ShapeSettings::ShapeResult baseResult;

  switch (desc.kind) {
    case PhysicsShapeKind::Plane: {
      const float normalLenSq = glm::dot(desc.normal, desc.normal);
      const Vector3 unitNormal = (normalLenSq > kEpsilonSq)
                                   ? glm::normalize(desc.normal)
                                   : Vector3(0.0f, 1.0f, 0.0f);
      const float halfExtent = std::max(desc.planeHalfExtent, 1.0f);
      const JPH::PlaneShapeSettings settings(
        JPH::Plane(toJoltVec3(unitNormal), 0.0f), nullptr, halfExtent);
      baseResult = settings.Create();
      break;
    }
    case PhysicsShapeKind::Box: {
      const Vector3 clampedExtents(std::max(desc.halfExtents.x, kMinExtent),
                                   std::max(desc.halfExtents.y, kMinExtent),
                                   std::max(desc.halfExtents.z, kMinExtent));
      const float convexRadius =
        std::min(
          { clampedExtents.x, clampedExtents.y, clampedExtents.z, 0.05f }) *
        0.5f;
      const JPH::BoxShapeSettings settings(toJoltVec3(clampedExtents),
                                           convexRadius);
      baseResult = settings.Create();
      break;
    }
    case PhysicsShapeKind::Sphere: {
      const float radius = std::max(desc.radius, kMinExtent);
      const JPH::SphereShapeSettings settings(radius);
      baseResult = settings.Create();
      break;
    }
    case PhysicsShapeKind::Capsule: {
      const float radius = std::max(desc.radius, kMinExtent);
      const float minHeight = 2.0f * radius;
      const float totalHeight = std::max(desc.height, minHeight);
      const float halfCylinderHeight = 0.5f * (totalHeight - minHeight);
      if (halfCylinderHeight <= 1.0e-4f) {
        const JPH::SphereShapeSettings settings(radius);
        baseResult = settings.Create();
      } else {
        const JPH::CapsuleShapeSettings settings(halfCylinderHeight, radius);
        baseResult = settings.Create();
      }
      break;
    }
  }

  if (baseResult.HasError()) {
    error = baseResult.GetError().c_str();
    return nullptr;
  }

  JPH::RefConst<JPH::Shape> shape = baseResult.Get();
  const JPH::Vec3 localOffset = toJoltVec3(desc.offset);
  const JPH::Quat localRotation = toJoltQuat(desc.rotationOffset);
  const bool hasOffset = localOffset.LengthSq() > kEpsilonSq;
  const bool hasRotation =
    !localRotation.IsClose(JPH::Quat::sIdentity(), 1.0e-6f);
  if (hasOffset || hasRotation) {
    const JPH::RotatedTranslatedShapeSettings offsetSettings(
      localOffset, localRotation, shape);
    const JPH::ShapeSettings::ShapeResult offsetResult =
      offsetSettings.Create();
    if (offsetResult.HasError()) {
      error = offsetResult.GetError().c_str();
      return nullptr;
    }
    shape = offsetResult.Get();
  }

  return shape;
}

struct PhysicsWorld::Impl
{
  PhysicsBroadPhaseLayerMap broadPhaseLayers{};
  PhysicsObjectVsBroadPhaseFilter objectVsBroadPhaseFilter{};
  PhysicsObjectLayerPairFilter objectLayerPairFilter{};
  PhysicsSolidObjectLayerFilter solidObjectFilter{};
  PhysicsAllObjectLayerFilter allObjectFilter{};
  JPH::BroadPhaseLayerFilter defaultBroadPhaseFilter{};
  JPH::BodyFilter defaultBodyFilter{};
  JPH::ShapeFilter defaultShapeFilter{};
  PhysicsSensorContactListener sensorListener{ 256 };
  JPH::CharacterVsCharacterCollisionSimple characterCollision{};

  std::unique_ptr<JPH::TempAllocator> tempAllocator{};
  std::unique_ptr<JPH::JobSystemSingleThreaded> jobSystem{};
  std::unique_ptr<JPH::PhysicsSystem> system{};

  std::vector<PhysicsBodyReverseEntry> reverseLookup{};
  std::vector<PhysicsBodySlot> bodySlots{};
  std::vector<uint32_t> freeBodySlots{};
  std::vector<PhysicsCharacterSlot> characterSlots{};
  std::vector<uint32_t> freeCharacterSlots{};
  std::vector<PhysicsOverlapPair> sensorOverlaps{};

  uint32_t nextCharacterId = 1;
  bool initialized = false;
  bool ownsJoltRef = false;

  const PhysicsBodySlot* resolveBody(PhysicsBodyHandle handle) const noexcept
  {
    if (!initialized || !handle.isValid() || handle.slot >= bodySlots.size()) {
      return nullptr;
    }
    const PhysicsBodySlot& slot = bodySlots[handle.slot];
    if (!slot.alive || slot.generation != handle.generation) {
      return nullptr;
    }
    return &slot;
  }

  PhysicsBodySlot* resolveBodyMutable(PhysicsBodyHandle handle) noexcept
  {
    if (!initialized || !handle.isValid() || handle.slot >= bodySlots.size()) {
      return nullptr;
    }
    PhysicsBodySlot& slot = bodySlots[handle.slot];
    if (!slot.alive || slot.generation != handle.generation) {
      return nullptr;
    }
    return &slot;
  }

  const PhysicsCharacterSlot* resolveCharacter(
    PhysicsCharacterHandle handle) const noexcept
  {
    if (!initialized || !handle.isValid() ||
        handle.slot >= characterSlots.size()) {
      return nullptr;
    }
    const PhysicsCharacterSlot& slot = characterSlots[handle.slot];
    if (!slot.alive || slot.generation != handle.generation) {
      return nullptr;
    }
    return &slot;
  }

  PhysicsCharacterSlot* resolveCharacterMutable(
    PhysicsCharacterHandle handle) noexcept
  {
    if (!initialized || !handle.isValid() ||
        handle.slot >= characterSlots.size()) {
      return nullptr;
    }
    PhysicsCharacterSlot& slot = characterSlots[handle.slot];
    if (!slot.alive || slot.generation != handle.generation) {
      return nullptr;
    }
    return &slot;
  }

  void registerReverseBody(JPH::BodyID bodyId,
                           PhysicsBodyHandle bodyHandle,
                           PhysicsCharacterHandle characterHandle,
                           uint64_t userData) noexcept
  {
    if (bodyId.IsInvalid()) {
      return;
    }
    const uint32_t index = bodyId.GetIndex();
    if (index < reverseLookup.size()) {
      PhysicsBodyReverseEntry& entry = reverseLookup[index];
      entry.body = bodyHandle;
      entry.character = characterHandle;
      entry.userData = userData;
      entry.occupied = true;
    }
  }

  void unregisterReverseBody(JPH::BodyID bodyId) noexcept
  {
    if (bodyId.IsInvalid()) {
      return;
    }
    const uint32_t index = bodyId.GetIndex();
    if (index < reverseLookup.size()) {
      reverseLookup[index] = PhysicsBodyReverseEntry{};
    }
  }

  void rebuildSensorOverlaps() noexcept
  {
    sensorOverlaps.clear();
    const std::vector<PhysicsActiveSensorSubShapePair>& contacts =
      sensorListener.activeContacts();
    for (const PhysicsActiveSensorSubShapePair& contact : contacts) {
      const uint32_t sensorIndex = contact.sensorBodyId.GetIndex();
      const uint32_t otherIndex = contact.otherBodyId.GetIndex();
      if (sensorIndex >= reverseLookup.size() ||
          otherIndex >= reverseLookup.size()) {
        continue;
      }
      const PhysicsBodyReverseEntry& sensorEntry = reverseLookup[sensorIndex];
      const PhysicsBodyReverseEntry& otherEntry = reverseLookup[otherIndex];
      if (!sensorEntry.occupied || !otherEntry.occupied ||
          !sensorEntry.body.isValid()) {
        continue;
      }

      bool duplicate = false;
      for (const PhysicsOverlapPair& existing : sensorOverlaps) {
        if (existing.sensorBody == sensorEntry.body &&
            existing.otherBody == otherEntry.body &&
            existing.otherCharacter == otherEntry.character) {
          duplicate = true;
          break;
        }
      }
      if (duplicate) {
        continue;
      }

      PhysicsOverlapPair pair;
      pair.sensorBody = sensorEntry.body;
      pair.otherBody = otherEntry.body;
      pair.otherCharacter = otherEntry.character;
      pair.sensorUserData = sensorEntry.userData;
      pair.otherUserData = otherEntry.userData;
      sensorOverlaps.push_back(pair);
    }
  }
};

PhysicsWorld::PhysicsWorld()
  : m_impl(std::make_unique<Impl>())
{
}

PhysicsWorld::~PhysicsWorld()
{
  shutdown();
}

bool
PhysicsWorld::initialize(const PhysicsWorldConfig& config, std::string& error)
{
  shutdown();

  if (config.maxBodies == 0 || config.maxBodyPairs == 0 ||
      config.maxContactConstraints == 0) {
    error = "PhysicsWorldConfig capacities must be non-zero";
    return false;
  }

  retainJoltGlobals();
  m_impl->ownsJoltRef = true;

  const size_t arenaBytes =
    std::max<size_t>(config.tempArenaBytes, 256u * 1024u);
#ifdef JPH_DISABLE_TEMP_ALLOCATOR
  (void)arenaBytes;
  m_impl->tempAllocator = std::make_unique<JPH::TempAllocatorMalloc>();
#else
  m_impl->tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(arenaBytes);
#endif
  m_impl->jobSystem =
    std::make_unique<JPH::JobSystemSingleThreaded>(kMaxPhysicsJobs);
  m_impl->system = std::make_unique<JPH::PhysicsSystem>();
  m_impl->system->Init(config.maxBodies,
                       0,
                       config.maxBodyPairs,
                       config.maxContactConstraints,
                       m_impl->broadPhaseLayers,
                       m_impl->objectVsBroadPhaseFilter,
                       m_impl->objectLayerPairFilter);
  m_impl->system->SetGravity(toJoltVec3(config.gravity));
  JPH::PhysicsSettings solverSettings = m_impl->system->GetPhysicsSettings();
  solverSettings.mPenetrationSlop = std::max(config.penetrationSlop, 0.0005f);
  solverSettings.mSpeculativeContactDistance =
    std::max(solverSettings.mPenetrationSlop * 5.0f, 0.01f);
  m_impl->system->SetPhysicsSettings(solverSettings);
  m_impl->system->SetContactListener(&m_impl->sensorListener);

  m_impl->reverseLookup.assign(config.maxBodies, PhysicsBodyReverseEntry{});
  m_impl->bodySlots.clear();
  m_impl->bodySlots.reserve(128);
  m_impl->freeBodySlots.clear();
  m_impl->freeBodySlots.reserve(64);
  m_impl->characterSlots.clear();
  m_impl->characterSlots.reserve(16);
  m_impl->freeCharacterSlots.clear();
  m_impl->freeCharacterSlots.reserve(16);
  m_impl->sensorOverlaps.clear();
  m_impl->sensorOverlaps.reserve(64);
  m_impl->sensorListener.clear();
  m_impl->characterCollision.mCharacters.clear();
  m_impl->nextCharacterId = 1;
  m_impl->initialized = true;
  return true;
}

bool
PhysicsWorld::initialize(std::string& error)
{
  const PhysicsWorldConfig defaultConfig{};
  return initialize(defaultConfig, error);
}

void
PhysicsWorld::shutdown() noexcept
{
  if (!m_impl) {
    return;
  }

  if (m_impl->initialized && m_impl->system) {
    // Destroy virtual characters before PhysicsSystem so each character's
    // destructor can cleanly remove its optional inner rigid body.
    m_impl->characterCollision.mCharacters.clear();
    for (PhysicsCharacterSlot& slot : m_impl->characterSlots) {
      if (slot.alive && slot.character != nullptr) {
        m_impl->unregisterReverseBody(slot.character->GetInnerBodyID());
        slot.character = nullptr;
        slot.alive = false;
      }
    }

    JPH::BodyInterface& bodyInterface = m_impl->system->GetBodyInterface();
    for (PhysicsBodySlot& slot : m_impl->bodySlots) {
      if (slot.alive && !slot.bodyId.IsInvalid()) {
        m_impl->unregisterReverseBody(slot.bodyId);
        bodyInterface.RemoveBody(slot.bodyId);
        bodyInterface.DestroyBody(slot.bodyId);
        slot.bodyId = JPH::BodyID();
        slot.alive = false;
      }
    }
  }

  m_impl->sensorListener.clear();
  m_impl->sensorOverlaps.clear();
  m_impl->reverseLookup.clear();
  m_impl->bodySlots.clear();
  m_impl->freeBodySlots.clear();
  m_impl->characterSlots.clear();
  m_impl->freeCharacterSlots.clear();

  m_impl->system.reset();
  m_impl->jobSystem.reset();
  m_impl->tempAllocator.reset();
  m_impl->initialized = false;

  if (m_impl->ownsJoltRef) {
    m_impl->ownsJoltRef = false;
    releaseJoltGlobals();
  }
}

bool
PhysicsWorld::isInitialized() const noexcept
{
  return m_impl && m_impl->initialized;
}

void
PhysicsWorld::setGravity(const Vector3& gravity) noexcept
{
  if (m_impl && m_impl->initialized && m_impl->system) {
    m_impl->system->SetGravity(toJoltVec3(gravity));
  }
}

Vector3
PhysicsWorld::gravity() const noexcept
{
  if (!m_impl || !m_impl->initialized || !m_impl->system) {
    return Vector3(0.0f, 0.0f, 0.0f);
  }
  return fromJoltVec3(m_impl->system->GetGravity());
}

PhysicsBodyHandle
PhysicsWorld::createBody(const PhysicsBodyDesc& desc, std::string& error)
{
  if (!m_impl || !m_impl->initialized || !m_impl->system) {
    error = "PhysicsWorld is not initialized";
    return PhysicsBodyHandle{};
  }
  if (desc.shape.kind == PhysicsShapeKind::Plane &&
      desc.motion == PhysicsMotionKind::Dynamic) {
    error = "Plane colliders cannot use dynamic motion";
    return PhysicsBodyHandle{};
  }

  const JPH::RefConst<JPH::Shape> shape = buildJoltShape(desc.shape, error);
  if (shape == nullptr) {
    return PhysicsBodyHandle{};
  }

  JPH::EMotionType motionType = JPH::EMotionType::Static;
  JPH::ObjectLayer objectLayer = kLayerNonMoving;
  JPH::EActivation activation = JPH::EActivation::DontActivate;

  if (desc.isSensor) {
    objectLayer = kLayerSensor;
    if (desc.shape.kind == PhysicsShapeKind::Plane) {
      motionType = JPH::EMotionType::Static;
    } else {
      motionType = JPH::EMotionType::Kinematic;
      activation = JPH::EActivation::Activate;
    }
  } else {
    switch (desc.motion) {
      case PhysicsMotionKind::Static:
        motionType = JPH::EMotionType::Static;
        objectLayer = kLayerNonMoving;
        activation = JPH::EActivation::DontActivate;
        break;
      case PhysicsMotionKind::Kinematic:
        motionType = JPH::EMotionType::Kinematic;
        objectLayer = kLayerMoving;
        activation = JPH::EActivation::Activate;
        break;
      case PhysicsMotionKind::Dynamic:
        motionType = JPH::EMotionType::Dynamic;
        objectLayer = kLayerMoving;
        activation = JPH::EActivation::Activate;
        break;
    }
  }

  JPH::BodyCreationSettings settings(shape,
                                     toJoltRVec3(desc.position),
                                     toJoltQuat(desc.rotation),
                                     motionType,
                                     objectLayer);
  settings.mFriction = std::max(desc.friction, 0.0f);
  settings.mRestitution = std::clamp(desc.restitution, 0.0f, 1.0f);
  settings.mLinearDamping = std::max(desc.linearDamping, 0.0f);
  settings.mAngularDamping = std::max(desc.angularDamping, 0.0f);
  settings.mGravityFactor = desc.gravityFactor;
  settings.mIsSensor = desc.isSensor;
  settings.mUserData = desc.userData;

  if (desc.isSensor && motionType == JPH::EMotionType::Kinematic) {
    settings.mCollideKinematicVsNonDynamic = true;
    settings.mAllowSleeping = false;
  }

  if (motionType == JPH::EMotionType::Dynamic) {
    settings.mOverrideMassProperties =
      JPH::EOverrideMassProperties::CalculateInertia;
    settings.mMassPropertiesOverride.mMass = std::max(desc.mass, 0.001f);

    JPH::EAllowedDOFs allowedDofs = JPH::EAllowedDOFs::TranslationX |
                                    JPH::EAllowedDOFs::TranslationY |
                                    JPH::EAllowedDOFs::TranslationZ;
    if (!desc.lockRotationX) {
      allowedDofs |= JPH::EAllowedDOFs::RotationX;
    }
    if (!desc.lockRotationY) {
      allowedDofs |= JPH::EAllowedDOFs::RotationY;
    }
    if (!desc.lockRotationZ) {
      allowedDofs |= JPH::EAllowedDOFs::RotationZ;
    }
    settings.mAllowedDOFs = allowedDofs;
  }

  JPH::BodyInterface& bodyInterface = m_impl->system->GetBodyInterface();
  const JPH::BodyID bodyId =
    bodyInterface.CreateAndAddBody(settings, activation);
  if (bodyId.IsInvalid()) {
    error = "PhysicsWorld body capacity exhausted";
    return PhysicsBodyHandle{};
  }

  uint32_t slotIndex = 0;
  if (!m_impl->freeBodySlots.empty()) {
    slotIndex = m_impl->freeBodySlots.back();
    m_impl->freeBodySlots.pop_back();
  } else {
    slotIndex = static_cast<uint32_t>(m_impl->bodySlots.size());
    m_impl->bodySlots.push_back(PhysicsBodySlot{});
  }

  PhysicsBodySlot& slot = m_impl->bodySlots[slotIndex];
  slot.bodyId = bodyId;
  slot.userData = desc.userData;
  slot.alive = true;

  const PhysicsBodyHandle handle{ slotIndex, slot.generation };
  m_impl->registerReverseBody(
    bodyId, handle, PhysicsCharacterHandle{}, desc.userData);
  return handle;
}

void
PhysicsWorld::destroyBody(PhysicsBodyHandle handle) noexcept
{
  if (!m_impl) {
    return;
  }
  PhysicsBodySlot* slot = m_impl->resolveBodyMutable(handle);
  if (slot == nullptr) {
    return;
  }

  const JPH::BodyID bodyId = slot->bodyId;
  m_impl->sensorListener.removeBody(bodyId);
  m_impl->unregisterReverseBody(bodyId);
  m_impl->rebuildSensorOverlaps();

  JPH::BodyInterface& bodyInterface = m_impl->system->GetBodyInterface();
  bodyInterface.RemoveBody(bodyId);
  bodyInterface.DestroyBody(bodyId);

  slot->bodyId = JPH::BodyID();
  slot->userData = 0;
  slot->alive = false;
  ++slot->generation;
  if (slot->generation == 0) {
    slot->generation = 1;
  }
  m_impl->freeBodySlots.push_back(handle.slot);
}

bool
PhysicsWorld::isBodyValid(PhysicsBodyHandle handle) const noexcept
{
  return m_impl && m_impl->resolveBody(handle) != nullptr;
}

Vector3
PhysicsWorld::getBodyPosition(PhysicsBodyHandle handle) const noexcept
{
  const PhysicsBodySlot* slot = m_impl ? m_impl->resolveBody(handle) : nullptr;
  if (slot == nullptr) {
    return Vector3(0.0f, 0.0f, 0.0f);
  }
  const JPH::BodyInterface& bodyInterface = m_impl->system->GetBodyInterface();
  return fromJoltRVec3(bodyInterface.GetPosition(slot->bodyId));
}

Quaternion
PhysicsWorld::getBodyRotation(PhysicsBodyHandle handle) const noexcept
{
  const PhysicsBodySlot* slot = m_impl ? m_impl->resolveBody(handle) : nullptr;
  if (slot == nullptr) {
    return Quaternion(1.0f, 0.0f, 0.0f, 0.0f);
  }
  const JPH::BodyInterface& bodyInterface = m_impl->system->GetBodyInterface();
  return fromJoltQuat(bodyInterface.GetRotation(slot->bodyId));
}

Transform3D
PhysicsWorld::getBodyTransform(PhysicsBodyHandle handle) const noexcept
{
  Transform3D transform;
  const PhysicsBodySlot* slot = m_impl ? m_impl->resolveBody(handle) : nullptr;
  if (slot == nullptr) {
    return transform;
  }
  const JPH::BodyInterface& bodyInterface = m_impl->system->GetBodyInterface();
  JPH::RVec3 position = JPH::RVec3::sZero();
  JPH::Quat rotation = JPH::Quat::sIdentity();
  bodyInterface.GetPositionAndRotation(slot->bodyId, position, rotation);
  transform.position = fromJoltRVec3(position);
  transform.rotation = fromJoltQuat(rotation);
  return transform;
}

void
PhysicsWorld::setBodyPose(PhysicsBodyHandle handle,
                          const Vector3& position,
                          const Quaternion& rotation,
                          bool activate) noexcept
{
  const PhysicsBodySlot* slot = m_impl ? m_impl->resolveBody(handle) : nullptr;
  if (slot == nullptr) {
    return;
  }
  JPH::BodyInterface& bodyInterface = m_impl->system->GetBodyInterface();
  bodyInterface.SetPositionAndRotation(
    slot->bodyId,
    toJoltRVec3(position),
    toJoltQuat(rotation),
    activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
}

void
PhysicsWorld::moveKinematicBody(PhysicsBodyHandle handle,
                                const Vector3& targetPosition,
                                const Quaternion& targetRotation,
                                float deltaSeconds) noexcept
{
  const PhysicsBodySlot* slot = m_impl ? m_impl->resolveBody(handle) : nullptr;
  if (slot == nullptr || deltaSeconds <= 0.0f) {
    return;
  }
  JPH::BodyInterface& bodyInterface = m_impl->system->GetBodyInterface();
  bodyInterface.MoveKinematic(slot->bodyId,
                              toJoltRVec3(targetPosition),
                              toJoltQuat(targetRotation),
                              deltaSeconds);
}

Vector3
PhysicsWorld::getBodyLinearVelocity(PhysicsBodyHandle handle) const noexcept
{
  const PhysicsBodySlot* slot = m_impl ? m_impl->resolveBody(handle) : nullptr;
  if (slot == nullptr) {
    return Vector3(0.0f, 0.0f, 0.0f);
  }
  const JPH::BodyInterface& bodyInterface = m_impl->system->GetBodyInterface();
  return fromJoltVec3(bodyInterface.GetLinearVelocity(slot->bodyId));
}

void
PhysicsWorld::setBodyLinearVelocity(PhysicsBodyHandle handle,
                                    const Vector3& velocity) noexcept
{
  const PhysicsBodySlot* slot = m_impl ? m_impl->resolveBody(handle) : nullptr;
  if (slot == nullptr) {
    return;
  }
  JPH::BodyInterface& bodyInterface = m_impl->system->GetBodyInterface();
  bodyInterface.SetLinearVelocity(slot->bodyId, toJoltVec3(velocity));
}

void
PhysicsWorld::addImpulse(PhysicsBodyHandle handle,
                         const Vector3& impulse) noexcept
{
  const PhysicsBodySlot* slot = m_impl ? m_impl->resolveBody(handle) : nullptr;
  if (slot == nullptr) {
    return;
  }
  JPH::BodyInterface& bodyInterface = m_impl->system->GetBodyInterface();
  bodyInterface.AddImpulse(slot->bodyId, toJoltVec3(impulse));
}

PhysicsCharacterHandle
PhysicsWorld::createCharacter(const PhysicsCharacterDesc& desc,
                              std::string& error)
{
  if (!m_impl || !m_impl->initialized || !m_impl->system) {
    error = "PhysicsWorld is not initialized";
    return PhysicsCharacterHandle{};
  }

  const float radius = std::max(desc.radius, kMinExtent);
  const float minHeight = 2.0f * radius;
  const float totalHeight = std::max(desc.height, minHeight);
  const float halfCylinderHeight = 0.5f * (totalHeight - minHeight);

  PhysicsShapeDesc shapeDesc = PhysicsShapeDesc::capsule(radius, totalHeight);
  const JPH::RefConst<JPH::Shape> shape = buildJoltShape(shapeDesc, error);
  if (shape == nullptr) {
    return PhysicsCharacterHandle{};
  }

  JPH::CharacterVirtualSettings settings;
  settings.mID = JPH::CharacterID(m_impl->nextCharacterId++);
  settings.mUp = JPH::Vec3::sAxisY();
  // In character-local space (centered at the capsule's midpoint), contacts on
  // or below the lower hemisphere center (-halfCylinderHeight) can support the
  // character; side-wall and ceiling contacts remain non-supporting.
  settings.mSupportingVolume =
    JPH::Plane(JPH::Vec3::sAxisY(), halfCylinderHeight);
  settings.mMaxSlopeAngle =
    JPH::DegreesToRadians(std::clamp(desc.maxSlopeDegrees, 0.0f, 89.0f));
  settings.mEnhancedInternalEdgeRemoval = true;
  settings.mShape = shape;
  settings.mMass = std::max(desc.mass, 0.0f);
  settings.mMaxStrength = std::max(desc.maxStrength, 0.0f);
  settings.mCharacterPadding = std::max(desc.characterPadding, 0.001f);
  settings.mInnerBodyShape = shape;
  settings.mInnerBodyLayer = kLayerMoving;

  JPH::Ref<JPH::CharacterVirtual> character =
    new JPH::CharacterVirtual(&settings,
                              toJoltRVec3(desc.position),
                              toJoltQuat(desc.rotation),
                              desc.userData,
                              m_impl->system.get());
  character->SetCharacterVsCharacterCollision(&m_impl->characterCollision);
  m_impl->characterCollision.Add(character.GetPtr());

  character->RefreshContacts(m_impl->defaultBroadPhaseFilter,
                             m_impl->solidObjectFilter,
                             m_impl->defaultBodyFilter,
                             m_impl->defaultShapeFilter,
                             *m_impl->tempAllocator);

  uint32_t slotIndex = 0;
  if (!m_impl->freeCharacterSlots.empty()) {
    slotIndex = m_impl->freeCharacterSlots.back();
    m_impl->freeCharacterSlots.pop_back();
  } else {
    slotIndex = static_cast<uint32_t>(m_impl->characterSlots.size());
    m_impl->characterSlots.push_back(PhysicsCharacterSlot{});
  }

  PhysicsCharacterSlot& slot = m_impl->characterSlots[slotIndex];
  slot.character = character;
  slot.extendedSettings.mStickToFloorStepDown =
    JPH::Vec3(0.0f, -std::max(desc.stickToFloorStepDown, 0.0f), 0.0f);
  slot.extendedSettings.mWalkStairsStepUp =
    JPH::Vec3(0.0f, std::max(desc.stepUpHeight, 0.0f), 0.0f);
  slot.userData = desc.userData;
  slot.alive = true;

  const PhysicsCharacterHandle handle{ slotIndex, slot.generation };
  m_impl->registerReverseBody(
    character->GetInnerBodyID(), PhysicsBodyHandle{}, handle, desc.userData);
  return handle;
}

void
PhysicsWorld::destroyCharacter(PhysicsCharacterHandle handle) noexcept
{
  if (!m_impl) {
    return;
  }
  PhysicsCharacterSlot* slot = m_impl->resolveCharacterMutable(handle);
  if (slot == nullptr) {
    return;
  }

  if (slot->character != nullptr) {
    const JPH::BodyID innerBodyId = slot->character->GetInnerBodyID();
    m_impl->sensorListener.removeBody(innerBodyId);
    m_impl->unregisterReverseBody(innerBodyId);
    m_impl->rebuildSensorOverlaps();
    m_impl->characterCollision.Remove(slot->character.GetPtr());
    slot->character = nullptr;
  }

  slot->userData = 0;
  slot->alive = false;
  ++slot->generation;
  if (slot->generation == 0) {
    slot->generation = 1;
  }
  m_impl->freeCharacterSlots.push_back(handle.slot);
}

bool
PhysicsWorld::isCharacterValid(PhysicsCharacterHandle handle) const noexcept
{
  return m_impl && m_impl->resolveCharacter(handle) != nullptr;
}

Vector3
PhysicsWorld::getCharacterPosition(PhysicsCharacterHandle handle) const noexcept
{
  const PhysicsCharacterSlot* slot =
    m_impl ? m_impl->resolveCharacter(handle) : nullptr;
  if (slot == nullptr || slot->character == nullptr) {
    return Vector3(0.0f, 0.0f, 0.0f);
  }
  return fromJoltRVec3(slot->character->GetPosition());
}

void
PhysicsWorld::setCharacterPosition(PhysicsCharacterHandle handle,
                                   const Vector3& position) noexcept
{
  PhysicsCharacterSlot* slot =
    m_impl ? m_impl->resolveCharacterMutable(handle) : nullptr;
  if (slot == nullptr || slot->character == nullptr) {
    return;
  }
  slot->character->SetPosition(toJoltRVec3(position));
  slot->character->RefreshContacts(m_impl->defaultBroadPhaseFilter,
                                   m_impl->solidObjectFilter,
                                   m_impl->defaultBodyFilter,
                                   m_impl->defaultShapeFilter,
                                   *m_impl->tempAllocator);
}

Quaternion
PhysicsWorld::getCharacterRotation(PhysicsCharacterHandle handle) const noexcept
{
  const PhysicsCharacterSlot* slot =
    m_impl ? m_impl->resolveCharacter(handle) : nullptr;
  if (slot == nullptr || slot->character == nullptr) {
    return Quaternion(1.0f, 0.0f, 0.0f, 0.0f);
  }
  return fromJoltQuat(slot->character->GetRotation());
}

void
PhysicsWorld::setCharacterRotation(PhysicsCharacterHandle handle,
                                   const Quaternion& rotation) noexcept
{
  PhysicsCharacterSlot* slot =
    m_impl ? m_impl->resolveCharacterMutable(handle) : nullptr;
  if (slot == nullptr || slot->character == nullptr) {
    return;
  }
  slot->character->SetRotation(toJoltQuat(rotation));
}

Vector3
PhysicsWorld::getCharacterLinearVelocity(
  PhysicsCharacterHandle handle) const noexcept
{
  const PhysicsCharacterSlot* slot =
    m_impl ? m_impl->resolveCharacter(handle) : nullptr;
  if (slot == nullptr || slot->character == nullptr) {
    return Vector3(0.0f, 0.0f, 0.0f);
  }
  return fromJoltVec3(slot->character->GetLinearVelocity());
}

void
PhysicsWorld::setCharacterLinearVelocity(PhysicsCharacterHandle handle,
                                         const Vector3& velocity) noexcept
{
  PhysicsCharacterSlot* slot =
    m_impl ? m_impl->resolveCharacterMutable(handle) : nullptr;
  if (slot == nullptr || slot->character == nullptr) {
    return;
  }
  slot->character->SetLinearVelocity(toJoltVec3(velocity));
}

Vector3
PhysicsWorld::cancelVelocityTowardsSteepSlopes(
  PhysicsCharacterHandle handle,
  const Vector3& desiredVelocity) const noexcept
{
  const PhysicsCharacterSlot* slot =
    m_impl ? m_impl->resolveCharacter(handle) : nullptr;
  if (slot == nullptr || slot->character == nullptr) {
    return desiredVelocity;
  }
  return fromJoltVec3(slot->character->CancelVelocityTowardsSteepSlopes(
    toJoltVec3(desiredVelocity)));
}

PhysicsCharacterGroundState
PhysicsWorld::getCharacterGroundState(
  PhysicsCharacterHandle handle) const noexcept
{
  const PhysicsCharacterSlot* slot =
    m_impl ? m_impl->resolveCharacter(handle) : nullptr;
  if (slot == nullptr || slot->character == nullptr) {
    return PhysicsCharacterGroundState::InAir;
  }
  switch (slot->character->GetGroundState()) {
    case JPH::CharacterBase::EGroundState::OnGround:
      return PhysicsCharacterGroundState::Grounded;
    case JPH::CharacterBase::EGroundState::OnSteepGround:
      return PhysicsCharacterGroundState::OnSteepGround;
    case JPH::CharacterBase::EGroundState::NotSupported:
      return PhysicsCharacterGroundState::NotSupported;
    case JPH::CharacterBase::EGroundState::InAir:
      return PhysicsCharacterGroundState::InAir;
  }
  return PhysicsCharacterGroundState::InAir;
}

bool
PhysicsWorld::isCharacterGrounded(PhysicsCharacterHandle handle) const noexcept
{
  return getCharacterGroundState(handle) ==
         PhysicsCharacterGroundState::Grounded;
}

void
PhysicsWorld::refreshCharacterContacts(PhysicsCharacterHandle handle) noexcept
{
  PhysicsCharacterSlot* slot =
    m_impl ? m_impl->resolveCharacterMutable(handle) : nullptr;
  if (slot == nullptr || slot->character == nullptr) {
    return;
  }
  slot->character->RefreshContacts(m_impl->defaultBroadPhaseFilter,
                                   m_impl->solidObjectFilter,
                                   m_impl->defaultBodyFilter,
                                   m_impl->defaultShapeFilter,
                                   *m_impl->tempAllocator);
}

Vector3
PhysicsWorld::getCharacterGroundNormal(
  PhysicsCharacterHandle handle) const noexcept
{
  const PhysicsCharacterSlot* slot =
    m_impl ? m_impl->resolveCharacter(handle) : nullptr;
  if (slot == nullptr || slot->character == nullptr) {
    return Vector3(0.0f, 1.0f, 0.0f);
  }
  return fromJoltVec3(slot->character->GetGroundNormal());
}

Vector3
PhysicsWorld::getCharacterGroundVelocity(
  PhysicsCharacterHandle handle) const noexcept
{
  const PhysicsCharacterSlot* slot =
    m_impl ? m_impl->resolveCharacter(handle) : nullptr;
  if (slot == nullptr || slot->character == nullptr) {
    return Vector3(0.0f, 0.0f, 0.0f);
  }
  return fromJoltVec3(slot->character->GetGroundVelocity());
}

void
PhysicsWorld::step(float deltaSeconds, int collisionSteps)
{
  if (!m_impl || !m_impl->initialized || !m_impl->system ||
      deltaSeconds <= 0.0f) {
    return;
  }

  const int clampedSteps = std::max(collisionSteps, 1);
  const JPH::Vec3 worldGravity = m_impl->system->GetGravity();

  for (PhysicsCharacterSlot& slot : m_impl->characterSlots) {
    if (!slot.alive || slot.character == nullptr) {
      continue;
    }
    slot.character->ExtendedUpdate(deltaSeconds,
                                   worldGravity,
                                   slot.extendedSettings,
                                   m_impl->defaultBroadPhaseFilter,
                                   m_impl->solidObjectFilter,
                                   m_impl->defaultBodyFilter,
                                   m_impl->defaultShapeFilter,
                                   *m_impl->tempAllocator);
  }

  m_impl->system->Update(deltaSeconds,
                         clampedSteps,
                         m_impl->tempAllocator.get(),
                         m_impl->jobSystem.get());
  m_impl->rebuildSensorOverlaps();
}

bool
PhysicsWorld::raycast(const Vector3& origin,
                      const Vector3& direction,
                      float maxDistance,
                      PhysicsRayHit& outHit,
                      bool includeSensors) const
{
  outHit = PhysicsRayHit{};
  if (!m_impl || !m_impl->initialized || !m_impl->system ||
      maxDistance <= 0.0f) {
    return false;
  }

  const float dirLengthSq = glm::dot(direction, direction);
  if (dirLengthSq <= kEpsilonSq) {
    return false;
  }

  const Vector3 unitDirection = direction / std::sqrt(dirLengthSq);
  const Vector3 rayDelta = unitDirection * maxDistance;
  const JPH::RRayCast ray(toJoltRVec3(origin), toJoltVec3(rayDelta));
  JPH::RayCastResult result;

  const JPH::ObjectLayerFilter& layerFilter =
    includeSensors
      ? static_cast<const JPH::ObjectLayerFilter&>(m_impl->allObjectFilter)
      : static_cast<const JPH::ObjectLayerFilter&>(m_impl->solidObjectFilter);

  if (!m_impl->system->GetNarrowPhaseQuery().CastRay(
        ray,
        result,
        m_impl->defaultBroadPhaseFilter,
        layerFilter,
        m_impl->defaultBodyFilter)) {
    return false;
  }

  const JPH::RVec3 hitPoint = ray.GetPointOnRay(result.mFraction);
  outHit.point = fromJoltRVec3(hitPoint);
  outHit.fraction = result.mFraction;
  outHit.distance = result.mFraction * maxDistance;

  {
    const JPH::BodyLockRead lock(m_impl->system->GetBodyLockInterface(),
                                 result.mBodyID);
    if (lock.Succeeded()) {
      const JPH::Body& body = lock.GetBody();
      outHit.normal = fromJoltVec3(
        body.GetWorldSpaceSurfaceNormal(result.mSubShapeID2, hitPoint));
    }
  }

  const uint32_t bodyIndex = result.mBodyID.GetIndex();
  if (bodyIndex < m_impl->reverseLookup.size()) {
    const PhysicsBodyReverseEntry& entry = m_impl->reverseLookup[bodyIndex];
    if (entry.occupied) {
      outHit.body = entry.body;
      outHit.character = entry.character;
      outHit.userData = entry.userData;
    }
  }
  return true;
}

std::span<const PhysicsOverlapPair>
PhysicsWorld::sensorOverlaps() const noexcept
{
  if (!m_impl || !m_impl->initialized) {
    return {};
  }
  return m_impl->sensorOverlaps;
}
