include_guard(GLOBAL)

# IllumoGame exists only as a WASM package hosted by IllumoRuntime. The pinned
# Wasmtime/WASI SDK pair is Windows x64 only, so other hosts build the engine
# and tools without a game.
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(_illumo_wasm_default ON)
else()
  set(_illumo_wasm_default OFF)
endif()
option(ILLUMO_BUILD_WASM_RUNTIME
  "Build IllumoRuntime and the IllumoGame WASM package" ${_illumo_wasm_default})
if(NOT ILLUMO_BUILD_WASM_RUNTIME)
  return()
endif()
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "The pinned WASM runtime currently requires Windows x64")
endif()
set(ILLUMO_WASM_TOOLS "${CMAKE_SOURCE_DIR}/build-wasm-tools" CACHE PATH
  "Directory populated by tools/bootstrap-wasm.ps1")
set(_wasmtime "${ILLUMO_WASM_TOOLS}/wasmtime-v48.0.2-x86_64-windows-c-api")
set(_wasi_sdk "${ILLUMO_WASM_TOOLS}/wasi-sdk-34.0-x86_64-windows")
foreach(_required "${_wasmtime}/include/wasmtime.h"
    "${_wasmtime}/lib/wasmtime.dll.lib" "${_wasmtime}/lib/wasmtime.dll"
    "${_wasi_sdk}/bin/clang++.exe")
  if(NOT EXISTS "${_required}")
    message(FATAL_ERROR "Missing ${_required}; run tools/bootstrap-wasm.ps1 "
      "explicitly, or configure with -DILLUMO_BUILD_WASM_RUNTIME=OFF to build "
      "without IllumoRuntime and the IllumoGame package")
  endif()
endforeach()

add_library(IllumoWasmtime SHARED IMPORTED GLOBAL)
set_target_properties(IllumoWasmtime PROPERTIES
  IMPORTED_IMPLIB "${_wasmtime}/lib/wasmtime.dll.lib"
  IMPORTED_LOCATION "${_wasmtime}/lib/wasmtime.dll"
  INTERFACE_INCLUDE_DIRECTORIES "${_wasmtime}/include")
add_library(IllumoWasmRuntime STATIC
  "${CMAKE_SOURCE_DIR}/Illumo/Source/Wasm/WasmInstance.cpp"
  "${CMAKE_SOURCE_DIR}/Illumo/Source/Wasm/WasmGuest.cpp"
  "${CMAKE_SOURCE_DIR}/Illumo/Source/Wasm/WasmWorker.cpp"
  "${CMAKE_SOURCE_DIR}/Illumo/Source/Platform/Windows/WinWasmCompiler.cpp")
add_library(Illumo::WasmRuntime ALIAS IllumoWasmRuntime)
target_include_directories(IllumoWasmRuntime PUBLIC "${CMAKE_SOURCE_DIR}/Illumo/Include")
target_include_directories(IllumoWasmRuntime PUBLIC "${CMAKE_SOURCE_DIR}/IllumoGuest/Include")
target_link_libraries(IllumoWasmRuntime PRIVATE IllumoWasmtime)
illumo_configure_runtime_target(IllumoWasmRuntime)
add_executable(IllumoWasmCompiler
  "${CMAKE_SOURCE_DIR}/Illumo/Source/Platform/Windows/WinWasmCompilerMain.cpp")
target_link_libraries(IllumoWasmCompiler PRIVATE IllumoWasmtime)
# No sanitizer: the helper runs no product code, and ASan's allocator would
# only inflate Wasmtime's compilation inside the job's memory limit (a Debug
# game package then exceeded it). Its engine settings still follow the build
# configuration, so artifacts match the host's.
illumo_configure_cpp_target(IllumoWasmCompiler)
add_custom_command(TARGET IllumoWasmCompiler POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:IllumoWasmtime>" "$<TARGET_FILE_DIR:IllumoWasmCompiler>" VERBATIM)
add_dependencies(IllumoWasmRuntime IllumoWasmCompiler)

add_library(IllumoWasmRendering STATIC
  "${CMAKE_SOURCE_DIR}/Illumo/Source/Wasm/WasmFrameRenderer.cpp"
  "${CMAKE_SOURCE_DIR}/Illumo/Source/Wasm/WasmRenderServices.cpp"
  "${CMAKE_SOURCE_DIR}/Illumo/Source/Wasm/WasmGameServices.cpp"
  "${CMAKE_SOURCE_DIR}/Illumo/Source/Wasm/WasmFileServices.cpp"
  "${CMAKE_SOURCE_DIR}/Illumo/Source/Wasm/WasmGameModule.cpp")
target_link_libraries(IllumoWasmRendering PUBLIC Illumo::WasmRuntime Illumo::Illumo)
illumo_configure_runtime_target(IllumoWasmRendering)

include(ExternalProject)
set(_guest_build "${CMAKE_BINARY_DIR}/wasm-guests")
ExternalProject_Add(IllumoGuestBuild
  SOURCE_DIR "${CMAKE_SOURCE_DIR}/IllumoGuest"
  BINARY_DIR "${_guest_build}"
  CMAKE_GENERATOR Ninja
  CMAKE_ARGS "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_SOURCE_DIR}/cmake/IllumoWasiToolchain.cmake"
    "-DILLUMO_WASM_TOOLS=${ILLUMO_WASM_TOOLS}" -DCMAKE_BUILD_TYPE=Release
  BUILD_ALWAYS TRUE
  BUILD_BYPRODUCTS "${_guest_build}/CSimDomainGuest.wasm" "${_guest_build}/PaddleGuest.wasm" "${_guest_build}/CSimWorkerGuest.wasm"
    "${_guest_build}/PaddlePaletteMod.wasm" "${_guest_build}/PaddleFaultyMod.wasm"
    "${_guest_build}/PresentationGuest.wasm"
    "${_guest_build}/JobControlGuest.wasm"
    "${_guest_build}/FileControlGuest.wasm"
    "${_guest_build}/SdkContractGuest.wasm"
    "${_guest_build}/IllumoGame.wasm"
    "${_guest_build}/IllEd.wasm"
    "${_guest_build}/IllMeshViewer.wasm"
  INSTALL_COMMAND "")

# Generic host executable. It contains no product code; installed applications
# are staged beside it in apps/<name>/, each described by an app.json manifest.
add_executable(IllumoRuntime $<TARGET_OBJECTS:IllumoPlatformEntry>
  "${CMAKE_SOURCE_DIR}/Illumo/Source/Wasm/RuntimeApplication.cpp")
target_link_libraries(IllumoRuntime PRIVATE IllumoWasmRendering)
target_include_directories(IllumoRuntime SYSTEM PRIVATE
  "${CMAKE_SOURCE_DIR}/Illumo/thirdparty/json/single_include")
illumo_configure_runtime_target(IllumoRuntime)
illumo_stage_runtime(IllumoRuntime)
illumo_stage_default_file(IllumoRuntime
  "${CMAKE_SOURCE_DIR}/Illumo/Source/Wasm/RuntimeEnvVars.json" envvars.json)
illumo_stage_msvc_asan(IllumoRuntime)
add_custom_command(TARGET IllumoRuntime POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:IllumoWasmtime>" "$<TARGET_FILE_DIR:IllumoRuntime>"
  VERBATIM)

# Stages one installed application: apps/<name>/ holds its manifest, module
# and flat data files. ASSETS lists source/destination pairs whose
# destinations are package-relative paths (package preloads such as
# Assets/IllEd/editor-ui-atlas.jpg).
function(illumo_stage_app target name)
  cmake_parse_arguments(PARSE_ARGV 2 _app "" "MODULE" "FILES;ASSETS")
  set(_package "$<TARGET_FILE_DIR:IllumoRuntime>/apps/${name}")
  set(_commands
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_package}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${_guest_build}/${_app_MODULE}" ${_app_FILES} "${_package}")
  set(_inputs ${_app_FILES})
  list(LENGTH _app_ASSETS _asset_values)
  math(EXPR _asset_odd "${_asset_values} % 2")
  if(_asset_odd)
    message(FATAL_ERROR "illumo_stage_app ASSETS takes source/destination pairs")
  endif()
  while(_app_ASSETS)
    list(POP_FRONT _app_ASSETS _asset_source _asset_destination)
    get_filename_component(_asset_directory "${_asset_destination}" DIRECTORY)
    list(APPEND _commands
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_package}/${_asset_directory}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_asset_source}" "${_package}/${_asset_destination}")
    list(APPEND _inputs "${_asset_source}")
  endwhile()
  add_custom_target(${target} ${_commands}
    DEPENDS ${_inputs}
    VERBATIM
    COMMENT "Staging the ${name} application package")
  set_target_properties(${target} PROPERTIES FOLDER "staging")
  add_dependencies(${target} IllumoGuestBuild)
  add_dependencies(IllumoRuntime ${target})
endfunction()

# IllumoGame: manifest, product module and packaged catalogs.
illumo_stage_app(IllumoGamePackage game
  MODULE IllumoGame.wasm
  FILES
    "${CMAKE_SOURCE_DIR}/IllumoGame/app.json"
    "${CMAKE_SOURCE_DIR}/IllumoGame/families.json"
    "${CMAKE_SOURCE_DIR}/IllumoGame/rulesets.json"
    "${CMAKE_SOURCE_DIR}/IllumoGame/envvars.json")

# IllEd: the world editor; its UI atlas is preloaded from the package.
illumo_stage_app(IllEdPackage illed
  MODULE IllEd.wasm
  FILES
    "${CMAKE_SOURCE_DIR}/IllEd/app.json"
    "${CMAKE_SOURCE_DIR}/IllEd/envvars.json"
  ASSETS
    "${CMAKE_SOURCE_DIR}/IllEd/Assets/editor-ui-atlas.jpg"
    "Assets/IllEd/editor-ui-atlas.jpg")

# IllMeshViewer: the mesh viewer; its skybox cross is preloaded.
illumo_stage_app(IllMeshViewerPackage meshviewer
  MODULE IllMeshViewer.wasm
  FILES
    "${CMAKE_SOURCE_DIR}/IllMeshViewer/app.json"
    "${CMAKE_SOURCE_DIR}/IllMeshViewer/envvars.json"
  ASSETS
    "${CMAKE_SOURCE_DIR}/Illumo/Assets/Skybox/skybox-daylight.png"
    "Assets/Skybox/skybox-daylight.png")

if(BUILD_TESTING)
  # Runtime command line: help and option rejection finish before any window
  # opens, so they are headless. tools/verify_capture.py covers real captures.
  add_test(NAME Illumo.Runtime.Help COMMAND IllumoRuntime --help)
  add_test(NAME Illumo.Runtime.InvalidCaptureFrame
    COMMAND IllumoRuntime --capture-frame 0)
  set_tests_properties(Illumo.Runtime.InvalidCaptureFrame PROPERTIES WILL_FAIL TRUE)
  set_tests_properties(Illumo.Runtime.Help Illumo.Runtime.InvalidCaptureFrame
    PROPERTIES LABELS "Illumo;IllumoWorkspace")
  add_dependencies(IllumoRunTests IllumoRuntime)

  add_executable(IllumoWasmFileTests "${CMAKE_SOURCE_DIR}/Illumo/Tests/Wasm/TestWasmFiles.cpp")
  target_link_libraries(IllumoWasmFileTests PRIVATE IllumoWasmRendering Illumo::TestSupport)
  illumo_configure_runtime_target(IllumoWasmFileTests)
  illumo_stage_msvc_asan(IllumoWasmFileTests)
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/Testing/WasmFiles")
  add_test(NAME Illumo.Wasm.FileServices COMMAND IllumoWasmFileTests --run Illumo.Wasm.FileServices)
  set_tests_properties(Illumo.Wasm.FileServices PROPERTIES LABELS "Illumo;IllumoWorkspace" TIMEOUT 20 WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Testing/WasmFiles")
  add_dependencies(IllumoRunTests IllumoWasmFileTests)
  add_executable(IllumoWasmFrameTests "${CMAKE_SOURCE_DIR}/Illumo/Tests/Wasm/TestWasmFrame.cpp")
  target_link_libraries(IllumoWasmFrameTests PRIVATE IllumoWasmRendering Illumo::TestSupport)
  target_compile_definitions(IllumoWasmFrameTests PRIVATE "ILLUMO_PADDLE_GUEST=\"${_guest_build}/PaddleGuest.wasm\"")
  target_compile_definitions(IllumoWasmFrameTests PRIVATE
    "ILLUMO_ENGINE_ASSETS=\"${CMAKE_SOURCE_DIR}/Illumo/Assets\""
    "ILLUMO_PRESENTATION_GUEST=\"${_guest_build}/PresentationGuest.wasm\""
    "ILLUMO_SDK_CONTRACT_GUEST=\"${_guest_build}/SdkContractGuest.wasm\""
    "ILLUMO_FILE_CONTROL_GUEST=\"${_guest_build}/FileControlGuest.wasm\""
    "ILLUMO_JOB_CONTROL_GUEST=\"${_guest_build}/JobControlGuest.wasm\""
    "ILLUMO_JOB_WORKER_GUEST=\"${CMAKE_BINARY_DIR}/wasm-tests/compatibility.wasm\""
    "ILLUMO_PALETTE_MOD=\"${_guest_build}/PaddlePaletteMod.wasm\""
    "ILLUMO_FAULTY_MOD=\"${_guest_build}/PaddleFaultyMod.wasm\"")
  add_dependencies(IllumoWasmFrameTests IllumoGuestBuild)
  illumo_configure_runtime_target(IllumoWasmFrameTests)
  illumo_stage_msvc_asan(IllumoWasmFrameTests)
  foreach(_case FrameValidation FrameRendering FrameFailures GameHost ModIsolation RenderServices GuestPresentation GameJobs SdkContract GameFiles DisplayServices ClipboardServices ConsoleServices DialogServices RetainedResources)
    add_test(NAME "Illumo.Wasm.${_case}" COMMAND IllumoWasmFrameTests --run "Illumo.Wasm.${_case}")
    set_tests_properties("Illumo.Wasm.${_case}" PROPERTIES LABELS "Illumo;IllumoWorkspace" TIMEOUT 20 WORKING_DIRECTORY "$<TARGET_FILE_DIR:IllumoWasmFrameTests>")
  endforeach()
  add_dependencies(IllumoRunTests IllumoWasmFrameTests)
  set(_guest_source "${CMAKE_SOURCE_DIR}/Illumo/Tests/Wasm/CompatibilityGuest.cpp")
  set(_guest_binary "${CMAKE_BINARY_DIR}/wasm-tests/compatibility.wasm")
  add_custom_command(OUTPUT "${_guest_binary}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/wasm-tests"
    COMMAND "${_wasi_sdk}/bin/clang++.exe" "${_guest_source}"
      "-I${CMAKE_SOURCE_DIR}/IllumoGuest/Include"
      -std=c++23 -O2 -msimd128 -fwasm-exceptions
      -mllvm -wasm-use-legacy-eh=false -mexec-model=reactor
      -Wl,--export=compatibility -Wl,--export=counter
      -Wl,--export=spin -Wl,--export=crash -Wl,--export=grow
      -Wl,--export=allocate -Wl,--export=release -lunwind
      -Wl,--export=allocationFailure
      -Wl,--export=illumo_guest_describe -Wl,--export=illumo_guest_alloc
      -Wl,--export=illumo_guest_free -Wl,--export=illumo_guest_job
      -Wl,--export=illumo_guest_result_size
      -o "${_guest_binary}"
    DEPENDS "${_guest_source}" "${CMAKE_SOURCE_DIR}/IllumoGuest/Include/IllumoGuest/Wire.h" VERBATIM)
  add_custom_target(IllumoWasmTestGuest DEPENDS "${_guest_binary}")
  add_dependencies(IllumoWasmFrameTests IllumoWasmTestGuest)
  set(_lifecycle_binary "${CMAKE_BINARY_DIR}/wasm-tests/lifecycle.wasm")
  add_custom_command(OUTPUT "${_lifecycle_binary}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/wasm-tests"
    COMMAND "${_wasi_sdk}/bin/clang++.exe"
      "${CMAKE_SOURCE_DIR}/Illumo/Tests/Wasm/LifecycleGuest.cpp"
      "-I${CMAKE_SOURCE_DIR}/IllumoGuest/Include"
      -std=c++23 -O2 -msimd128 -fwasm-exceptions
      -mllvm -wasm-use-legacy-eh=false -mexec-model=reactor -lunwind
      -Wl,--export=illumo_guest_describe -Wl,--export=illumo_guest_manifest
      -Wl,--export=illumo_guest_alloc -Wl,--export=illumo_guest_free
      -Wl,--export=illumo_guest_result_size -Wl,--export=illumo_guest_init
      -Wl,--export=illumo_guest_update -Wl,--export=illumo_guest_frame
      -Wl,--export=illumo_guest_close -Wl,--export=illumo_guest_shutdown
      -Wl,--export=illumo_guest_receive -o "${_lifecycle_binary}"
    DEPENDS "${CMAKE_SOURCE_DIR}/Illumo/Tests/Wasm/LifecycleGuest.cpp"
      "${CMAKE_SOURCE_DIR}/IllumoGuest/Include/IllumoGuest/Protocol.h"
      "${CMAKE_SOURCE_DIR}/IllumoGuest/Include/IllumoGuest/Wire.h" VERBATIM)
  add_custom_target(IllumoWasmLifecycleGuest DEPENDS "${_lifecycle_binary}")
  add_executable(IllumoWasmTests "${CMAKE_SOURCE_DIR}/Illumo/Tests/Wasm/TestWasmRuntime.cpp")
  target_link_libraries(IllumoWasmTests PRIVATE Illumo::WasmRuntime IllumoWasmtime)
  target_compile_definitions(IllumoWasmTests PRIVATE
    "ILLUMO_WASM_LIFECYCLE_GUEST=\"${_lifecycle_binary}\""
    "ILLUMO_WASM_TEST_GUEST=\"${_guest_binary}\"")
  illumo_configure_runtime_target(IllumoWasmTests)
  illumo_stage_msvc_asan(IllumoWasmTests)
  add_dependencies(IllumoWasmTests IllumoWasmTestGuest IllumoWasmLifecycleGuest)
  add_custom_command(TARGET IllumoWasmTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:IllumoWasmtime>" "$<TARGET_FILE_DIR:IllumoWasmTests>"
    VERBATIM)
  foreach(_case Compatibility Isolation Fuel Epoch Memory DeniedImports InvalidModule Worker Wire CompilerLimits Lifecycle Protocol Resources)
    add_test(NAME "Illumo.Wasm.${_case}" COMMAND IllumoWasmTests --run "Illumo.Wasm.${_case}")
    set_tests_properties("Illumo.Wasm.${_case}" PROPERTIES
      LABELS "Illumo;IllumoWorkspace" TIMEOUT 20)
  endforeach()
  add_dependencies(IllumoRunTests IllumoWasmTests)

  add_executable(IllumoGameWasmParityTests
    "${CMAKE_SOURCE_DIR}/IllumoGame/Tests/Wasm/TestDomainParity.cpp")
  target_link_libraries(IllumoGameWasmParityTests PRIVATE IllumoGameCore Illumo::WasmRuntime)
  target_compile_definitions(IllumoGameWasmParityTests PRIVATE
    "ILLUMO_DOMAIN_GUEST=\"${_guest_build}/CSimDomainGuest.wasm\""
    "ILLUMO_FAMILIES=\"${CMAKE_SOURCE_DIR}/IllumoGame/families.json\""
    "ILLUMO_RULES=\"${CMAKE_SOURCE_DIR}/IllumoGame/rulesets.json\"")
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
    "${CMAKE_SOURCE_DIR}/IllumoGame/Tests/Wasm/TestSimulationWorker.cpp"
    "${CMAKE_SOURCE_DIR}/IllumoGame/Source/Wasm/SimulationProtocol.cpp")
  target_link_libraries(IllumoGameWasmWorkerTests PRIVATE IllumoGameCore Illumo::WasmRuntime)
  target_compile_definitions(IllumoGameWasmWorkerTests PRIVATE
    "ILLUMO_SIMULATION_WORKER=\"${_guest_build}/CSimWorkerGuest.wasm\""
    "ILLUMO_FAMILIES=\"${CMAKE_SOURCE_DIR}/IllumoGame/families.json\""
    "ILLUMO_RULES=\"${CMAKE_SOURCE_DIR}/IllumoGame/rulesets.json\"")
  illumo_configure_runtime_target(IllumoGameWasmWorkerTests)
  illumo_stage_msvc_asan(IllumoGameWasmWorkerTests)
  add_dependencies(IllumoGameWasmWorkerTests IllumoGuestBuild)
  add_test(NAME IllumoGame.Wasm.WorkerParity COMMAND IllumoGameWasmWorkerTests --run IllumoGame.Wasm.WorkerParity)
  add_test(NAME IllumoGame.Wasm.SimulationProtocol COMMAND IllumoGameWasmWorkerTests --run IllumoGame.Wasm.SimulationProtocol)
  set_tests_properties(IllumoGame.Wasm.SimulationProtocol PROPERTIES LABELS "IllumoGame;IllumoWorkspace" TIMEOUT 30)
  set_tests_properties(IllumoGame.Wasm.WorkerParity PROPERTIES LABELS "IllumoGame;IllumoWorkspace" TIMEOUT 180)
  add_dependencies(IllumoRunTests IllumoGameWasmWorkerTests)


  # The complete product package driven through the generic host.
  add_executable(IllumoGameWasmPackageTests
    "${CMAKE_SOURCE_DIR}/IllumoGame/Tests/Wasm/TestGamePackage.cpp")
  target_link_libraries(IllumoGameWasmPackageTests PRIVATE
    IllumoGameCore IllumoWasmRendering Illumo::TestSupport)
  target_compile_definitions(IllumoGameWasmPackageTests PRIVATE
    "ILLUMO_GAME_GUEST=\"${_guest_build}/IllumoGame.wasm\""
    "ILLUMO_GAME_DEFAULTS=\"${CMAKE_SOURCE_DIR}/IllumoGame/envvars.json\""
    "ILLUMO_FAMILIES=\"${CMAKE_SOURCE_DIR}/IllumoGame/families.json\""
    "ILLUMO_RULES=\"${CMAKE_SOURCE_DIR}/IllumoGame/rulesets.json\"")
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
  add_dependencies(IllumoRunTests IllumoGameWasmPackageTests)

  # The IllEd package driven through the generic host: launch document,
  # package-preloaded atlas, keyboard edit and in-place save.
  add_executable(IllEdWasmPackageTests
    "${CMAKE_SOURCE_DIR}/IllEd/Tests/Wasm/TestEditorPackage.cpp")
  target_link_libraries(IllEdWasmPackageTests PRIVATE
    IllEdCore IllumoWasmRendering Illumo::TestSupport)
  target_compile_definitions(IllEdWasmPackageTests PRIVATE
    "ILLUMO_ILLED_GUEST=\"${_guest_build}/IllEd.wasm\""
    "ILLUMO_ILLED_DEFAULTS=\"${CMAKE_SOURCE_DIR}/IllEd/envvars.json\""
    "ILLUMO_ILLED_ATLAS=\"${CMAKE_SOURCE_DIR}/IllEd/Assets/editor-ui-atlas.jpg\"")
  illumo_configure_runtime_target(IllEdWasmPackageTests)
  illumo_stage_msvc_asan(IllEdWasmPackageTests)
  add_dependencies(IllEdWasmPackageTests IllumoGuestBuild)
  add_custom_command(TARGET IllEdWasmPackageTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:IllumoWasmtime>" "$<TARGET_FILE_DIR:IllEdWasmPackageTests>"
    VERBATIM)
  add_test(NAME IllEd.Wasm.Package
    COMMAND IllEdWasmPackageTests --run IllEd.Wasm.Package)
  set_tests_properties(IllEd.Wasm.Package PROPERTIES
    LABELS "IllEd;IllumoWorkspace" TIMEOUT 300
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:IllEdWasmPackageTests>")
  add_dependencies(IllumoRunTests IllEdWasmPackageTests)

  # The IllMeshViewer package: launch mesh as a retained host mesh (frame
  # schema v3) and the package-preloaded skybox as a host cubemap.
  add_executable(IllMeshViewerWasmPackageTests
    "${CMAKE_SOURCE_DIR}/IllMeshViewer/Tests/Wasm/TestViewerPackage.cpp")
  target_link_libraries(IllMeshViewerWasmPackageTests PRIVATE
    IllumoWasmRendering Illumo::TestSupport)
  target_compile_definitions(IllMeshViewerWasmPackageTests PRIVATE
    "ILLUMO_VIEWER_GUEST=\"${_guest_build}/IllMeshViewer.wasm\""
    "ILLUMO_VIEWER_DEFAULTS=\"${CMAKE_SOURCE_DIR}/IllMeshViewer/envvars.json\""
    "ILLUMO_VIEWER_SKYBOX=\"${CMAKE_SOURCE_DIR}/Illumo/Assets/Skybox/skybox-daylight.png\"")
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
  set_tests_properties(IllMeshViewer.Wasm.Package PROPERTIES
    LABELS "IllMeshViewer;IllumoWorkspace" TIMEOUT 300
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:IllMeshViewerWasmPackageTests>")
  add_dependencies(IllumoRunTests IllMeshViewerWasmPackageTests)
endif()
