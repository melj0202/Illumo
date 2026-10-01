# The Playground's package and its WASM tests, included by
# cmake/IllumoWasm.cmake (one entry of ILLUMO_PROGRAMS) once illumo_stage_app
# and IllumoRuntime exist.
set(_playground "${CMAKE_CURRENT_LIST_DIR}")

# The sample game: its behaviour descriptions and default scene ship with it.
illumo_stage_app(PlaygroundPackage playground
  MODULE Playground.wasm
  MANIFEST "${_playground}/illumo.json"
  FILES
    "${_playground}/envvars.json"
    "${_playground}/behaviours.json"
  ASSETS
    "${_playground}/Scenes/demo.ilsc"
    "Scenes/demo.ilsc")

if(BUILD_TESTING)
  # The package through the generic host: the default scene plays its
  # behaviours, and a launched scene plays with its own.
  add_executable(PlaygroundWasmPackageTests
    "${_playground}/Tests/Wasm/TestPlaygroundPackage.cpp")
  target_link_libraries(PlaygroundWasmPackageTests PRIVATE
    IllumoWasmRendering Illumo::Content Illumo::TestSupport)
  target_compile_definitions(PlaygroundWasmPackageTests PRIVATE
    "ILLUMO_PLAYGROUND_GUEST=\"${_guest_build}/Playground.wasm\""
    "ILLUMO_PLAYGROUND_SOURCE=\"${_playground}\"")
  illumo_configure_runtime_target(PlaygroundWasmPackageTests)
  illumo_stage_msvc_asan(PlaygroundWasmPackageTests)
  add_dependencies(PlaygroundWasmPackageTests IllumoGuestBuild)
  add_custom_command(TARGET PlaygroundWasmPackageTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:IllumoWasmtime>"
      "$<TARGET_FILE_DIR:PlaygroundWasmPackageTests>"
    VERBATIM)
  add_test(NAME Playground.Wasm.DefaultScene
    COMMAND PlaygroundWasmPackageTests --run Playground.Wasm.DefaultScene)
  add_test(NAME Playground.Wasm.LaunchedScene
    COMMAND PlaygroundWasmPackageTests --run Playground.Wasm.LaunchedScene)
  set_tests_properties(Playground.Wasm.DefaultScene
    Playground.Wasm.LaunchedScene PROPERTIES
    LABELS "Playground;IllumoWorkspace" TIMEOUT 300
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:PlaygroundWasmPackageTests>")
  add_dependencies(IllumoRunTests PlaygroundWasmPackageTests)
endif()
unset(_playground)
