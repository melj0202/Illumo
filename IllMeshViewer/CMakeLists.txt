cmake_minimum_required(VERSION 3.25)

add_library(IllMeshViewerCore STATIC
  Source/MeshViewerCamera.cpp
  Source/MeshViewerConfig.cpp
  Source/MeshViewerModule.cpp
  Source/MeshViewerPlatformNative.cpp
  Source/MeshViewerUi.cpp
  Source/IllMeshViewerApplication.cpp
)

target_include_directories(IllMeshViewerCore PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_include_directories(IllMeshViewerCore SYSTEM PRIVATE
  "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/tracy-0.13.1/public")
target_link_libraries(IllMeshViewerCore PUBLIC Illumo::Illumo)
illumo_configure_runtime_target(IllMeshViewerCore)

# IllMeshViewer ships only as the WASM package apps/meshviewer
# (cmake/IllumoWasm.cmake), played by IllumoRuntime. IllMeshViewerCore
# remains the native test oracle.

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
  illumo_stage_runtime(IllMeshViewerTests)
endif()
