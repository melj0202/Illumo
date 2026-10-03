# IllumoGame's package and its WASM tests, included by cmake/IllumoWasm.cmake
# (one entry of ILLUMO_PROGRAMS) once illumo_stage_app and IllumoRuntime exist.
set(_game "${CMAKE_CURRENT_LIST_DIR}")

# The package: manifest, product module, simulation lane worker and packaged
# catalogs.
# Sound effects ship as Sounds/<file>.wav and music as Music/<file>.mp3. Their
# sources in IllumoGame/Assets are not tracked in git, so a checkout without
# them builds a silent package.
set(_game_sounds)
foreach(_sound canvas_enter canvas_exit canvas_mode_switch
    canvas_paintmenu_collapse canvas_paintmenu_expand csim_program_start
    ui_menu_back ui_menu_error ui_menu_hover ui_menu_select)
  if(EXISTS "${_game}/Assets/${_sound}.wav")
    list(APPEND _game_sounds "${_game}/Assets/${_sound}.wav" "Sounds/${_sound}.wav")
  endif()
endforeach()
foreach(_track music_canvas_edit music_main_menu)
  if(EXISTS "${_game}/Assets/${_track}.mp3")
    list(APPEND _game_sounds "${_game}/Assets/${_track}.mp3" "Music/${_track}.mp3")
  endif()
endforeach()
illumo_stage_app(IllumoGamePackage game
  MODULE IllumoGame.wasm
  MANIFEST "${_game}/illumo.json"
  FILES
    "${_guest_build}/CSimWorkerGuest.wasm"
    "${_game}/families.json"
    "${_game}/rulesets.json"
    "${_game}/envvars.json"
  ASSETS
    "${_game}/Scenes/render3d-test.ilsc"
    "Scenes/render3d-test.ilsc"
    # app.ico at the package root replaces the engine's window icon (D-E39).
    "${CMAKE_SOURCE_DIR}/docs/brand/csim/ico/csim.ico"
    "app.ico"
    ${_game_sounds})
illumo_guest_byproducts("${_guest_build}/CSimDomainGuest.wasm")

if(BUILD_TESTING)
  add_executable(IllumoGameWasmParityTests
    "${_game}/Tests/Wasm/TestDomainParity.cpp")
  target_link_libraries(IllumoGameWasmParityTests PRIVATE IllumoGameCore Illumo::WasmRuntime)
  target_compile_definitions(IllumoGameWasmParityTests PRIVATE
    "ILLUMO_DOMAIN_GUEST=\"${_guest_build}/CSimDomainGuest.wasm\""
    "ILLUMO_FAMILIES=\"${_game}/families.json\""
    "ILLUMO_RULES=\"${_game}/rulesets.json\"")
  illumo_configure_runtime_target(IllumoGameWasmParityTests)
  illumo_stage_msvc_asan(IllumoGameWasmParityTests)
  add_dependencies(IllumoGameWasmParityTests IllumoGuestBuild)
  add_custom_command(TARGET IllumoGameWasmParityTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:IllumoWasmtime>" "$<TARGET_FILE_DIR:IllumoGameWasmParityTests>"
    VERBATIM)
  add_test(NAME IllumoGame.Wasm.DomainParity
    COMMAND IllumoGameWasmParityTests --run IllumoGame.Wasm.DomainParity)
  set_tests_properties(IllumoGame.Wasm.DomainParity PROPERTIES
    LABELS "IllumoGame;IllumoWorkspace" TIMEOUT 180)
  add_dependencies(IllumoRunTests IllumoGameWasmParityTests)

  add_executable(IllumoGameWasmWorkerTests
    "${_game}/Tests/Wasm/TestSimulationWorker.cpp"
    "${_game}/Source/Wasm/SimulationLanes.cpp"
    "${_game}/Source/Wasm/SimulationProtocol.cpp")
  target_link_libraries(IllumoGameWasmWorkerTests PRIVATE IllumoGameCore Illumo::WasmRuntime)
  target_compile_definitions(IllumoGameWasmWorkerTests PRIVATE
    "ILLUMO_SIMULATION_WORKER=\"${_guest_build}/CSimWorkerGuest.wasm\""
    "ILLUMO_FAMILIES=\"${_game}/families.json\""
    "ILLUMO_RULES=\"${_game}/rulesets.json\"")
  illumo_configure_runtime_target(IllumoGameWasmWorkerTests)
  illumo_stage_msvc_asan(IllumoGameWasmWorkerTests)
  add_dependencies(IllumoGameWasmWorkerTests IllumoGuestBuild)
  add_test(NAME IllumoGame.Wasm.WorkerParity COMMAND IllumoGameWasmWorkerTests --run IllumoGame.Wasm.WorkerParity)
  add_test(NAME IllumoGame.Wasm.SimulationProtocol COMMAND IllumoGameWasmWorkerTests --run IllumoGame.Wasm.SimulationProtocol)
  set_tests_properties(IllumoGame.Wasm.SimulationProtocol PROPERTIES LABELS "IllumoGame;IllumoWorkspace" TIMEOUT 30)
  add_test(NAME IllumoGame.Wasm.LaneParity COMMAND IllumoGameWasmWorkerTests --run IllumoGame.Wasm.LaneParity)
  add_test(NAME IllumoGame.Wasm.LaneProtocol COMMAND IllumoGameWasmWorkerTests --run IllumoGame.Wasm.LaneProtocol)
  # About 15 s in Release; the Debug ASan build needs several minutes.
  set_tests_properties(IllumoGame.Wasm.LaneParity PROPERTIES LABELS "IllumoGame;IllumoWorkspace" TIMEOUT 600)
  set_tests_properties(IllumoGame.Wasm.LaneProtocol PROPERTIES LABELS "IllumoGame;IllumoWorkspace" TIMEOUT 30)
  set_tests_properties(IllumoGame.Wasm.WorkerParity PROPERTIES LABELS "IllumoGame;IllumoWorkspace" TIMEOUT 180)
  add_test(NAME IllumoGame.Wasm.LaneAllocations COMMAND IllumoGameWasmWorkerTests --run IllumoGame.Wasm.LaneAllocations)
  set_tests_properties(IllumoGame.Wasm.LaneAllocations PROPERTIES LABELS "IllumoGame;IllumoWorkspace" TIMEOUT 120)
  add_dependencies(IllumoRunTests IllumoGameWasmWorkerTests)

  # The complete product package driven through the generic host.
  # The catalog bootstrap and the SDK file client also run natively here,
  # against the host file service, for the package catalog merge.
  add_executable(IllumoGameWasmPackageTests
    "${_game}/Tests/Wasm/TestGamePackage.cpp"
    "${_game}/Source/Wasm/CatalogBootstrap.cpp"
    "${CMAKE_SOURCE_DIR}/IllumoGuest/Source/Files.cpp")
  target_link_libraries(IllumoGameWasmPackageTests PRIVATE
    IllumoGameCore IllumoWasmRendering Illumo::TestSupport)
  target_compile_definitions(IllumoGameWasmPackageTests PRIVATE
    "ILLUMO_GAME_GUEST=\"${_guest_build}/IllumoGame.wasm\""
    "ILLUMO_SIMULATION_WORKER=\"${_guest_build}/CSimWorkerGuest.wasm\""
    "ILLUMO_GAME_DEFAULTS=\"${_game}/envvars.json\""
    "ILLUMO_FAMILIES=\"${_game}/families.json\""
    "ILLUMO_RULES=\"${_game}/rulesets.json\""
    "ILLUMO_RENDER3D_SCENE=\"${_game}/Scenes/render3d-test.ilsc\"")
  illumo_configure_runtime_target(IllumoGameWasmPackageTests)
  illumo_stage_msvc_asan(IllumoGameWasmPackageTests)
  add_dependencies(IllumoGameWasmPackageTests IllumoGuestBuild)
  add_custom_command(TARGET IllumoGameWasmPackageTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:IllumoWasmtime>" "$<TARGET_FILE_DIR:IllumoGameWasmPackageTests>"
    VERBATIM)
  add_test(NAME IllumoGame.Wasm.GamePackage
    COMMAND IllumoGameWasmPackageTests --run IllumoGame.Wasm.GamePackage)
  set_tests_properties(IllumoGame.Wasm.GamePackage PROPERTIES
    LABELS "IllumoGame;IllumoWorkspace" TIMEOUT 300
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:IllumoGameWasmPackageTests>")
  add_test(NAME IllumoGame.Wasm.GamePackageAudio
    COMMAND IllumoGameWasmPackageTests --run IllumoGame.Wasm.GamePackageAudio)
  set_tests_properties(IllumoGame.Wasm.GamePackageAudio PROPERTIES
    LABELS "IllumoGame;IllumoWorkspace" TIMEOUT 300
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:IllumoGameWasmPackageTests>")
  add_test(NAME IllumoGame.Wasm.GamePackageRestart
    COMMAND IllumoGameWasmPackageTests --run IllumoGame.Wasm.GamePackageRestart)
  set_tests_properties(IllumoGame.Wasm.GamePackageRestart PROPERTIES
    LABELS "IllumoGame;IllumoWorkspace" TIMEOUT 300
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:IllumoGameWasmPackageTests>")
  add_test(NAME IllumoGame.Wasm.CatalogMerge
    COMMAND IllumoGameWasmPackageTests --run IllumoGame.Wasm.CatalogMerge)
  set_tests_properties(IllumoGame.Wasm.CatalogMerge PROPERTIES
    LABELS "IllumoGame;IllumoWorkspace" TIMEOUT 60
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:IllumoGameWasmPackageTests>")
  add_test(NAME IllumoGame.Wasm.GamePackageLanes
    COMMAND IllumoGameWasmPackageTests --run IllumoGame.Wasm.GamePackageLanes)
  set_tests_properties(IllumoGame.Wasm.GamePackageLanes PROPERTIES
    LABELS "IllumoGame;IllumoWorkspace" TIMEOUT 300
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:IllumoGameWasmPackageTests>")
  add_test(NAME IllumoGame.Wasm.PackageFrameAllocations
    COMMAND IllumoGameWasmPackageTests
      --run IllumoGame.Wasm.PackageFrameAllocations)
  set_tests_properties(IllumoGame.Wasm.PackageFrameAllocations PROPERTIES
    LABELS "IllumoGame;IllumoWorkspace" TIMEOUT 300
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:IllumoGameWasmPackageTests>")
  # Performance measurement, not a workspace gate: ctest -L IllumoBenchmark.
  add_test(NAME IllumoGame.Wasm.PackageBench
    COMMAND IllumoGameWasmPackageTests --run IllumoGame.Wasm.PackageBench)
  set_tests_properties(IllumoGame.Wasm.PackageBench PROPERTIES
    LABELS "IllumoGame;IllumoBenchmark" TIMEOUT 900
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:IllumoGameWasmPackageTests>")
  add_dependencies(IllumoRunTests IllumoGameWasmPackageTests)
endif()
unset(_game)
unset(_game_sounds)
