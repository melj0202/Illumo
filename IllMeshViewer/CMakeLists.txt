cmake_minimum_required(VERSION 3.20)

add_library(IllMeshViewerCore STATIC
  Source/MeshViewerCamera.cpp
  Source/MeshViewerConfig.cpp
  Source/MeshViewerModule.cpp
  Source/MeshViewerUi.cpp
  Source/IllMeshViewerApplication.cpp
)

target_include_directories(IllMeshViewerCore PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_include_directories(IllMeshViewerCore SYSTEM PRIVATE
  "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/tracy-0.13.1/public")
target_link_libraries(IllMeshViewerCore PUBLIC Illumo::Illumo)
illumo_configure_runtime_target(IllMeshViewerCore)

add_executable(IllMeshViewer $<TARGET_OBJECTS:IllumoPlatformEntry>)
target_link_libraries(IllMeshViewer PRIVATE IllMeshViewerCore Illumo::Illumo)
illumo_configure_runtime_target(IllMeshViewer)
illumo_stage_runtime(IllMeshViewer)
illumo_stage_default_file(IllMeshViewer
  "${CMAKE_CURRENT_SOURCE_DIR}/envvars.json" envvars.json)
illumo_stage_msvc_asan(IllMeshViewer)

if(BUILD_TESTING)
  add_executable(IllMeshViewerTests
    Tests/TestMain.cpp
    Tests/TestMeshViewerCamera.cpp
    Tests/TestMeshViewerConfig.cpp
    Tests/TestMeshViewerModule.cpp
    Tests/TestMeshViewerUi.cpp
  )
  target_compile_definitions(IllMeshViewerTests PRIVATE ILLUMO_TESTING)
  target_include_directories(IllMeshViewerTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Tests")
  target_include_directories(IllMeshViewerTests SYSTEM PRIVATE
    "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/tracy-0.13.1/public")
  target_link_libraries(IllMeshViewerTests PRIVATE IllMeshViewerCore Illumo::TestSupport)
  illumo_configure_runtime_target(IllMeshViewerTests)
  illumo_discover_test_runner(IllMeshViewerTests IllMeshViewer)
  illumo_stage_msvc_asan(IllMeshViewerTests)
  add_dependencies(IllMeshViewerTests IllMeshViewerRuntimeStage)
endif()
