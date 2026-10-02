# IllMeshViewer's package and its WASM tests, included by
# cmake/IllumoWasm.cmake (one entry of ILLUMO_PROGRAMS) once illumo_stage_app
# and IllumoRuntime exist.
set(_viewer "${CMAKE_CURRENT_LIST_DIR}")

# The mesh viewer; it preloads the engine's skybox cross from /engine.
illumo_stage_app(IllMeshViewerPackage meshviewer
  MODULE IllMeshViewer.wasm
  MANIFEST "${_viewer}/illumo.json"
  FILES
    "${_viewer}/envvars.json")

if(BUILD_TESTING)
  # The package: launch mesh as a retained host mesh (frame schema v3) and the
  # skybox preloaded from /engine as a host cubemap.
  add_executable(IllMeshViewerWasmPackageTests
    "${_viewer}/Tests/Wasm/TestViewerPackage.cpp")
  target_link_libraries(IllMeshViewerWasmPackageTests PRIVATE
    IllumoWasmRendering Illumo::Content Illumo::TestSupport)
  target_compile_definitions(IllMeshViewerWasmPackageTests PRIVATE
    "ILLUMO_VIEWER_GUEST=\"${_guest_build}/IllMeshViewer.wasm\""
    "ILLUMO_VIEWER_DEFAULTS=\"${_viewer}/envvars.json\""
    "ILLUMO_ENGINE_ASSETS=\"${CMAKE_SOURCE_DIR}/Illumo/Assets\"")
  illumo_configure_runtime_target(IllMeshViewerWasmPackageTests)
  illumo_stage_msvc_asan(IllMeshViewerWasmPackageTests)
  add_dependencies(IllMeshViewerWasmPackageTests IllumoGuestBuild)
  add_custom_command(TARGET IllMeshViewerWasmPackageTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:IllumoWasmtime>"
      "$<TARGET_FILE_DIR:IllMeshViewerWasmPackageTests>"
    VERBATIM)
  add_test(NAME IllMeshViewer.Wasm.Package
    COMMAND IllMeshViewerWasmPackageTests --run IllMeshViewer.Wasm.Package)
  # A mounted content package: viewer_open fetches a scene and its mesh.
  add_test(NAME IllMeshViewer.Wasm.ScenePackage
    COMMAND IllMeshViewerWasmPackageTests --run IllMeshViewer.Wasm.ScenePackage)
  set_tests_properties(IllMeshViewer.Wasm.Package IllMeshViewer.Wasm.ScenePackage PROPERTIES
    LABELS "IllMeshViewer;IllumoWorkspace" TIMEOUT 300
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:IllMeshViewerWasmPackageTests>")
  add_dependencies(IllumoRunTests IllMeshViewerWasmPackageTests)
endif()
unset(_viewer)
