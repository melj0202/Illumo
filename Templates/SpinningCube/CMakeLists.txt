cmake_minimum_required(VERSION 3.20)

add_library(@PROJECT_NAME@Core STATIC
  Source/SpinningCubeApplication.cpp
  Source/SpinningCubeConfig.cpp
  Source/SpinningCubeModule.cpp
)
target_include_directories(@PROJECT_NAME@Core PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_include_directories(@PROJECT_NAME@Core SYSTEM PRIVATE
  "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/tracy-0.13.1/public")
target_link_libraries(@PROJECT_NAME@Core PUBLIC Illumo::Illumo)
illumo_configure_runtime_target(@PROJECT_NAME@Core)

add_executable(@PROJECT_NAME@ $<TARGET_OBJECTS:IllumoPlatformEntry>)
target_link_libraries(@PROJECT_NAME@ PRIVATE @PROJECT_NAME@Core Illumo::Illumo)
illumo_configure_runtime_target(@PROJECT_NAME@)
illumo_stage_runtime(@PROJECT_NAME@)
illumo_stage_default_file(@PROJECT_NAME@
  "${CMAKE_CURRENT_SOURCE_DIR}/envvars.json" envvars.json)
illumo_stage_msvc_asan(@PROJECT_NAME@)

if(BUILD_TESTING)
  add_executable(@PROJECT_NAME@Tests
    Tests/TestMain.cpp
    Tests/TestSpinningCubeModule.cpp
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
  add_dependencies(@PROJECT_NAME@Tests @PROJECT_NAME@RuntimeStage)
endif()
