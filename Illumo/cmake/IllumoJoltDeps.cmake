include_guard(GLOBAL)

# Third-party Jolt Physics 5.6.0 static library (docs/jolt-physics-plan.md).
# Included by Illumo/CMakeLists.txt (native) and IllumoGuest/CMakeLists.txt
# (WASI guest). Linked PRIVATE only to IllumoPhysics / IllumoGuestPhysics so
# Jolt headers, macros and symbols never enter Illumo::Illumo, Illumo::Content
# or downstream consumers.

set(_illumo_jolt_dir "${CMAKE_CURRENT_LIST_DIR}/../thirdparty/jolt-5.6.0")
set(_illumo_jolt "${_illumo_jolt_dir}/Jolt")

set(_illumo_jolt_sources
  "${_illumo_jolt}/AABBTree/AABBTreeBuilder.cpp"
  "${_illumo_jolt}/Core/Color.cpp"
  "${_illumo_jolt}/Core/Factory.cpp"
  "${_illumo_jolt}/Core/IssueReporting.cpp"
  "${_illumo_jolt}/Core/JobSystemSingleThreaded.cpp"
  "${_illumo_jolt}/Core/LinearCurve.cpp"
  "${_illumo_jolt}/Core/Memory.cpp"
  "${_illumo_jolt}/Core/Profiler.cpp"
  "${_illumo_jolt}/Core/RTTI.cpp"
  "${_illumo_jolt}/Core/StringTools.cpp"
  "${_illumo_jolt}/Core/TickCounter.cpp"
  "${_illumo_jolt}/Geometry/ConvexHullBuilder.cpp"
  "${_illumo_jolt}/Geometry/ConvexHullBuilder2D.cpp"
  "${_illumo_jolt}/Geometry/Indexify.cpp"
  "${_illumo_jolt}/Geometry/OrientedBox.cpp"
  "${_illumo_jolt}/Math/Vec3.cpp"
  "${_illumo_jolt}/ObjectStream/SerializableObject.cpp"
  "${_illumo_jolt}/Physics/Body/Body.cpp"
  "${_illumo_jolt}/Physics/Body/BodyCreationSettings.cpp"
  "${_illumo_jolt}/Physics/Body/BodyInterface.cpp"
  "${_illumo_jolt}/Physics/Body/BodyManager.cpp"
  "${_illumo_jolt}/Physics/Body/MassProperties.cpp"
  "${_illumo_jolt}/Physics/Body/MotionProperties.cpp"
  "${_illumo_jolt}/Physics/Character/Character.cpp"
  "${_illumo_jolt}/Physics/Character/CharacterBase.cpp"
  "${_illumo_jolt}/Physics/Character/CharacterVirtual.cpp"
  "${_illumo_jolt}/Physics/Collision/BroadPhase/BroadPhase.cpp"
  "${_illumo_jolt}/Physics/Collision/BroadPhase/BroadPhaseBruteForce.cpp"
  "${_illumo_jolt}/Physics/Collision/BroadPhase/BroadPhaseQuadTree.cpp"
  "${_illumo_jolt}/Physics/Collision/BroadPhase/QuadTree.cpp"
  "${_illumo_jolt}/Physics/Collision/CastConvexVsTriangles.cpp"
  "${_illumo_jolt}/Physics/Collision/CastSphereVsTriangles.cpp"
  "${_illumo_jolt}/Physics/Collision/CollideConvexVsTriangles.cpp"
  "${_illumo_jolt}/Physics/Collision/CollideSphereVsTriangles.cpp"
  "${_illumo_jolt}/Physics/Collision/CollisionDispatch.cpp"
  "${_illumo_jolt}/Physics/Collision/CollisionGroup.cpp"
  "${_illumo_jolt}/Physics/Collision/EstimateCollisionResponse.cpp"
  "${_illumo_jolt}/Physics/Collision/GroupFilter.cpp"
  "${_illumo_jolt}/Physics/Collision/GroupFilterTable.cpp"
  "${_illumo_jolt}/Physics/Collision/ManifoldBetweenTwoFaces.cpp"
  "${_illumo_jolt}/Physics/Collision/NarrowPhaseQuery.cpp"
  "${_illumo_jolt}/Physics/Collision/NarrowPhaseStats.cpp"
  "${_illumo_jolt}/Physics/Collision/PhysicsMaterial.cpp"
  "${_illumo_jolt}/Physics/Collision/PhysicsMaterialSimple.cpp"
  "${_illumo_jolt}/Physics/Collision/Shape/BoxShape.cpp"
  "${_illumo_jolt}/Physics/Collision/Shape/CapsuleShape.cpp"
  "${_illumo_jolt}/Physics/Collision/Shape/CompoundShape.cpp"
  "${_illumo_jolt}/Physics/Collision/Shape/ConvexHullShape.cpp"
  "${_illumo_jolt}/Physics/Collision/Shape/ConvexShape.cpp"
  "${_illumo_jolt}/Physics/Collision/Shape/CylinderShape.cpp"
  "${_illumo_jolt}/Physics/Collision/Shape/DecoratedShape.cpp"
  "${_illumo_jolt}/Physics/Collision/Shape/EmptyShape.cpp"
  "${_illumo_jolt}/Physics/Collision/Shape/HeightFieldShape.cpp"
  "${_illumo_jolt}/Physics/Collision/Shape/MeshShape.cpp"
  "${_illumo_jolt}/Physics/Collision/Shape/MutableCompoundShape.cpp"
  "${_illumo_jolt}/Physics/Collision/Shape/OffsetCenterOfMassShape.cpp"
  "${_illumo_jolt}/Physics/Collision/Shape/PlaneShape.cpp"
  "${_illumo_jolt}/Physics/Collision/Shape/RotatedTranslatedShape.cpp"
  "${_illumo_jolt}/Physics/Collision/Shape/ScaledShape.cpp"
  "${_illumo_jolt}/Physics/Collision/Shape/Shape.cpp"
  "${_illumo_jolt}/Physics/Collision/Shape/SphereShape.cpp"
  "${_illumo_jolt}/Physics/Collision/Shape/StaticCompoundShape.cpp"
  "${_illumo_jolt}/Physics/Collision/Shape/TaperedCapsuleShape.cpp"
  "${_illumo_jolt}/Physics/Collision/Shape/TaperedCylinderShape.cpp"
  "${_illumo_jolt}/Physics/Collision/Shape/TriangleShape.cpp"
  "${_illumo_jolt}/Physics/Collision/TransformedShape.cpp"
  "${_illumo_jolt}/Physics/Constraints/ConeConstraint.cpp"
  "${_illumo_jolt}/Physics/Constraints/Constraint.cpp"
  "${_illumo_jolt}/Physics/Constraints/ConstraintManager.cpp"
  "${_illumo_jolt}/Physics/Constraints/ContactConstraintManager.cpp"
  "${_illumo_jolt}/Physics/Constraints/DistanceConstraint.cpp"
  "${_illumo_jolt}/Physics/Constraints/FixedConstraint.cpp"
  "${_illumo_jolt}/Physics/Constraints/GearConstraint.cpp"
  "${_illumo_jolt}/Physics/Constraints/HingeConstraint.cpp"
  "${_illumo_jolt}/Physics/Constraints/MotorSettings.cpp"
  "${_illumo_jolt}/Physics/Constraints/PathConstraint.cpp"
  "${_illumo_jolt}/Physics/Constraints/PathConstraintPath.cpp"
  "${_illumo_jolt}/Physics/Constraints/PathConstraintPathHermite.cpp"
  "${_illumo_jolt}/Physics/Constraints/PointConstraint.cpp"
  "${_illumo_jolt}/Physics/Constraints/PulleyConstraint.cpp"
  "${_illumo_jolt}/Physics/Constraints/RackAndPinionConstraint.cpp"
  "${_illumo_jolt}/Physics/Constraints/SixDOFConstraint.cpp"
  "${_illumo_jolt}/Physics/Constraints/SliderConstraint.cpp"
  "${_illumo_jolt}/Physics/Constraints/SpringSettings.cpp"
  "${_illumo_jolt}/Physics/Constraints/SwingTwistConstraint.cpp"
  "${_illumo_jolt}/Physics/Constraints/TwoBodyConstraint.cpp"
  "${_illumo_jolt}/Physics/DeterminismLog.cpp"
  "${_illumo_jolt}/Physics/IslandBuilder.cpp"
  "${_illumo_jolt}/Physics/LargeIslandSplitter.cpp"
  "${_illumo_jolt}/Physics/PhysicsScene.cpp"
  "${_illumo_jolt}/Physics/PhysicsSystem.cpp"
  "${_illumo_jolt}/Physics/PhysicsUpdateContext.cpp"
  "${_illumo_jolt}/Physics/Ragdoll/Ragdoll.cpp"
  "${_illumo_jolt}/Physics/SoftBody/SoftBodyCreationSettings.cpp"
  "${_illumo_jolt}/Physics/SoftBody/SoftBodyMotionProperties.cpp"
  "${_illumo_jolt}/Physics/SoftBody/SoftBodyShape.cpp"
  "${_illumo_jolt}/Physics/SoftBody/SoftBodySharedSettings.cpp"
  "${_illumo_jolt}/Physics/StateRecorderImpl.cpp"
  "${_illumo_jolt}/Physics/Vehicle/MotorcycleController.cpp"
  "${_illumo_jolt}/Physics/Vehicle/TrackedVehicleController.cpp"
  "${_illumo_jolt}/Physics/Vehicle/VehicleAntiRollBar.cpp"
  "${_illumo_jolt}/Physics/Vehicle/VehicleCollisionTester.cpp"
  "${_illumo_jolt}/Physics/Vehicle/VehicleConstraint.cpp"
  "${_illumo_jolt}/Physics/Vehicle/VehicleController.cpp"
  "${_illumo_jolt}/Physics/Vehicle/VehicleDifferential.cpp"
  "${_illumo_jolt}/Physics/Vehicle/VehicleEngine.cpp"
  "${_illumo_jolt}/Physics/Vehicle/VehicleTrack.cpp"
  "${_illumo_jolt}/Physics/Vehicle/VehicleTransmission.cpp"
  "${_illumo_jolt}/Physics/Vehicle/Wheel.cpp"
  "${_illumo_jolt}/Physics/Vehicle/WheeledVehicleController.cpp"
  "${_illumo_jolt}/RegisterTypes.cpp"
  "${_illumo_jolt}/Renderer/DebugRenderer.cpp"
  "${_illumo_jolt}/Renderer/DebugRendererPlayback.cpp"
  "${_illumo_jolt}/Renderer/DebugRendererRecorder.cpp"
  "${_illumo_jolt}/Renderer/DebugRendererSimple.cpp"
  "${_illumo_jolt}/Skeleton/SkeletalAnimation.cpp"
  "${_illumo_jolt}/Skeleton/Skeleton.cpp"
  "${_illumo_jolt}/Skeleton/SkeletonMapper.cpp"
  "${_illumo_jolt}/Skeleton/SkeletonPose.cpp"
  "${_illumo_jolt}/TriangleSplitter/TriangleSplitter.cpp"
  "${_illumo_jolt}/TriangleSplitter/TriangleSplitterBinning.cpp"
  "${_illumo_jolt}/TriangleSplitter/TriangleSplitterMean.cpp")

add_library(illumo_jolt STATIC ${_illumo_jolt_sources})
target_include_directories(illumo_jolt SYSTEM PUBLIC "${_illumo_jolt_dir}")
# Feature macros participate in JPH_VERSION_ID, so both illumo_jolt and the
# private PhysicsWorld.cpp translation unit that links it must see the same set.
target_compile_definitions(illumo_jolt PUBLIC
  JPH_CROSS_PLATFORM_DETERMINISTIC
  JPH_NO_DEBUG)

if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
  # WASI SDK omits Emscripten's x86 <immintrin.h> shims; undefining
  # __wasm_simd128__ selects Jolt's portable float4 implementation while LLVM's
  # -msimd128 flag still lowers vectorizable loops to WASM SIMD128 instructions.
  target_compile_definitions(illumo_jolt PUBLIC JPH_PLATFORM_WASM NDEBUG)
  target_compile_options(illumo_jolt PUBLIC
    -msimd128
    -Wno-builtin-macro-redefined
    -U__wasm_simd128__
    -fno-exceptions)
  target_compile_options(illumo_jolt PRIVATE -O2 -w)
else()
  target_compile_features(illumo_jolt PRIVATE cxx_std_17)
  target_compile_definitions(illumo_jolt PUBLIC
    "$<$<NOT:$<CONFIG:Debug>>:NDEBUG>")
  if(ILLUMO_ENABLE_ASAN AND NOT ILLUMO_ENABLE_COVERAGE)
    target_compile_definitions(illumo_jolt PUBLIC
      "$<$<CONFIG:Debug>:JPH_DISABLE_TEMP_ALLOCATOR;JPH_DISABLE_CUSTOM_ALLOCATOR>")
  endif()
  if(MSVC)
    target_compile_options(illumo_jolt PRIVATE /W0 /MP
      $<$<CONFIG:Debug>:/Od>
      $<$<NOT:$<CONFIG:Debug>>:/O2>)
    if(ILLUMO_ENABLE_ASAN AND NOT ILLUMO_ENABLE_COVERAGE)
      target_compile_options(illumo_jolt PRIVATE
        $<$<CONFIG:Debug>:/fsanitize=address>)
    endif()
  else()
    if(UNIX)
      find_package(Threads REQUIRED)
      target_link_libraries(illumo_jolt PUBLIC Threads::Threads)
    endif()
    target_compile_options(illumo_jolt PRIVATE -w
      $<$<CONFIG:Debug>:-O0>
      $<$<NOT:$<CONFIG:Debug>>:-O2>)
    if(ILLUMO_ENABLE_ASAN AND NOT ILLUMO_ENABLE_COVERAGE)
      target_compile_options(illumo_jolt PRIVATE
        $<$<CONFIG:Debug>:-fsanitize=address>)
    endif()
  endif()
endif()
set_target_properties(illumo_jolt PROPERTIES FOLDER "thirdparty")

unset(_illumo_jolt_dir)
unset(_illumo_jolt)
unset(_illumo_jolt_sources)
