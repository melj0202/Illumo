# IllEd's package and its WASM tests, included by cmake/IllumoWasm.cmake (one
# entry of ILLUMO_PROGRAMS) once illumo_stage_app and IllumoRuntime exist.
set(_illed "${CMAKE_CURRENT_LIST_DIR}")

# The world editor; its UI atlas is preloaded from the package.
illumo_stage_app(IllEdPackage illed
  MODULE IllEd.wasm
  MANIFEST "${_illed}/illumo.json"
  FILES
    "${_illed}/envvars.json"
  ASSETS
    "${_illed}/Assets/editor-ui-atlas.jpg"
    "Assets/IllEd/editor-ui-atlas.jpg")

if(BUILD_TESTING)
  # A staged application packs into an .ilpk that reads back intact.
  add_test(NAME Illumo.Pack.StagedApp
    COMMAND ${CMAKE_COMMAND} "-DPACK=$<TARGET_FILE:IllumoPack>"
      "-DAPP=$<TARGET_FILE_DIR:IllumoRuntime>/apps/illed"
      "-DOUT=${CMAKE_BINARY_DIR}/Testing/illed.ilpk"
      -P "${CMAKE_SOURCE_DIR}/cmake/IllumoPackCheck.cmake")
  set_tests_properties(Illumo.Pack.StagedApp PROPERTIES
    LABELS "Illumo;IllumoWorkspace" TIMEOUT 60)
  add_dependencies(IllumoRunTests IllumoPack)

  # The IllEd package driven through the generic host: launch document,
  # package-preloaded atlas, keyboard edit and in-place save.
  add_executable(IllEdWasmPackageTests
    "${_illed}/Tests/Wasm/TestEditorPackage.cpp")
  target_link_libraries(IllEdWasmPackageTests PRIVATE
    IllEdCore IllumoWasmRendering Illumo::TestSupport)
  target_compile_definitions(IllEdWasmPackageTests PRIVATE
    "ILLUMO_ILLED_GUEST=\"${_guest_build}/IllEd.wasm\""
    "ILLUMO_ILLED_DEFAULTS=\"${_illed}/envvars.json\""
    "ILLUMO_ILLED_ATLAS=\"${_illed}/Assets/editor-ui-atlas.jpg\"")
  illumo_configure_runtime_target(IllEdWasmPackageTests)
  illumo_stage_msvc_asan(IllEdWasmPackageTests)
  add_dependencies(IllEdWasmPackageTests IllumoGuestBuild)
  add_custom_command(TARGET IllEdWasmPackageTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:IllumoWasmtime>" "$<TARGET_FILE_DIR:IllEdWasmPackageTests>"
    VERBATIM)
  add_test(NAME IllEd.Wasm.Package
    COMMAND IllEdWasmPackageTests --run IllEd.Wasm.Package)
  # With a writable /project: place project assets and save into the project.
  add_test(NAME IllEd.Wasm.ProjectPackage
    COMMAND IllEdWasmPackageTests --run IllEd.Wasm.ProjectPackage)
  # Play: an installed game's behaviours at /apps and a launch through the
  # Launch capability.
  add_test(NAME IllEd.Wasm.PlayPackage
    COMMAND IllEdWasmPackageTests --run IllEd.Wasm.PlayPackage)
  set_tests_properties(IllEd.Wasm.Package IllEd.Wasm.ProjectPackage
    IllEd.Wasm.PlayPackage PROPERTIES
    LABELS "IllEd;IllumoWorkspace" TIMEOUT 300
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:IllEdWasmPackageTests>")
  add_dependencies(IllumoRunTests IllEdWasmPackageTests)
endif()
unset(_illed)
