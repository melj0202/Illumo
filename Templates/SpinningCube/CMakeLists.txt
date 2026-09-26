cmake_minimum_required(VERSION 3.25)

# @PROJECT_NAME@ ships only as the WASM package apps/@PROJECT_ID@, played by
# IllumoRuntime: GuestTargets.cmake builds its module and PackageTargets.cmake
# stages it (the root CMakeLists.txt lists this directory in ILLUMO_PROGRAMS).
# This native library compiles the same scene against the host engine for the
# headless tests.
add_library(@PROJECT_NAME@Core STATIC
  Source/SpinningCubeConfig.cpp
  Source/SpinningCubeScene.cpp
)
target_include_directories(@PROJECT_NAME@Core PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_include_directories(@PROJECT_NAME@Core SYSTEM PRIVATE
  "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/tracy-0.13.1/public")
target_link_libraries(@PROJECT_NAME@Core PUBLIC Illumo::Illumo Illumo::Content)
illumo_configure_runtime_target(@PROJECT_NAME@Core)

if(BUILD_TESTING)
  add_executable(@PROJECT_NAME@Tests
    Tests/TestMain.cpp
    Tests/TestSpinningCubeScene.cpp
  )
  target_compile_definitions(@PROJECT_NAME@Tests PRIVATE ILLUMO_TESTING)
  target_include_directories(@PROJECT_NAME@Tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Tests")
  target_include_directories(@PROJECT_NAME@Tests SYSTEM PRIVATE
    "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/tracy-0.13.1/public")
  target_link_libraries(@PROJECT_NAME@Tests PRIVATE
    @PROJECT_NAME@Core Illumo::TestSupport)
  illumo_configure_runtime_target(@PROJECT_NAME@Tests)
  illumo_discover_test_runner(@PROJECT_NAME@Tests @PROJECT_NAME@)
  illumo_stage_msvc_asan(@PROJECT_NAME@Tests)
  illumo_stage_runtime(@PROJECT_NAME@Tests)
endif()
