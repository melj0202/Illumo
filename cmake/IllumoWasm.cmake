include_guard(GLOBAL)

option(ILLUMO_BUILD_WASM_RUNTIME "Build the isolated game runtime migration targets" OFF)
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
    message(FATAL_ERROR "Missing ${_required}; run tools/bootstrap-wasm.ps1 explicitly")
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
illumo_configure_runtime_target(IllumoWasmCompiler)
illumo_stage_msvc_asan(IllumoWasmCompiler)
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
  INSTALL_COMMAND "")

add_executable(IllumoWasmPlayer $<TARGET_OBJECTS:IllumoPlatformEntry>
  "${CMAKE_SOURCE_DIR}/Illumo/Source/Wasm/WasmPlayerApplication.cpp")
target_link_libraries(IllumoWasmPlayer PRIVATE IllumoWasmRendering)
illumo_configure_runtime_target(IllumoWasmPlayer)
illumo_stage_runtime(IllumoWasmPlayer)
illumo_stage_msvc_asan(IllumoWasmPlayer)
add_dependencies(IllumoWasmPlayer IllumoGuestBuild)

if(BUILD_TESTING)
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
  foreach(_case FrameValidation FrameRendering FrameFailures GameHost ModIsolation RenderServices GuestPresentation GameJobs SdkContract GameFiles DisplayServices ClipboardServices ConsoleServices DialogServices)
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
endif()
