cmake_minimum_required(VERSION 3.25)
project(IllumoGuest LANGUAGES C CXX)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "WASI")
  message(FATAL_ERROR "Use cmake/IllumoWasiToolchain.cmake in a separate guest build tree")
endif()
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
get_filename_component(ILLUMO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(ILLUMO_LIBRARY_SOURCE_DIR "${ILLUMO_ROOT}/Illumo")
include("${ILLUMO_ROOT}/Illumo/cmake/IllumoPublicMath.cmake")
illumo_prepare_public_math(_illumo_guest_math)

add_library(IllumoGuestOptions INTERFACE)
target_compile_options(IllumoGuestOptions INTERFACE
  -Wall -Wextra -msimd128 -fwasm-exceptions -mllvm -wasm-use-legacy-eh=false)
target_link_options(IllumoGuestOptions INTERFACE
  -msimd128 -fwasm-exceptions -mexec-model=reactor
  -Wl,-z,stack-size=1048576 -Wl,--max-memory=1073741824)
target_link_libraries(IllumoGuestOptions INTERFACE unwind)
# Guests are single-threaded: shared engine and product sources select their
# serial variants (no worker threads, locks or file-system paths).
target_compile_definitions(IllumoGuestOptions INTERFACE ILLUMO_SERIAL_GUEST)
target_include_directories(IllumoGuestOptions INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/Include")

function(illumo_add_guest target)
  add_executable(${target} Source/Exports.cpp ${ARGN})
  set_target_properties(${target} PROPERTIES SUFFIX ".wasm")
  target_link_libraries(${target} PRIVATE IllumoGuestOptions)
  target_link_options(${target} PRIVATE
    -Wl,--export=illumo_guest_describe -Wl,--export=illumo_guest_manifest
    -Wl,--export=illumo_guest_alloc -Wl,--export=illumo_guest_free
    -Wl,--export=illumo_guest_result_size -Wl,--export=illumo_guest_init
    -Wl,--export=illumo_guest_update -Wl,--export=illumo_guest_frame
    -Wl,--export=illumo_guest_receive -Wl,--export=illumo_guest_services
    -Wl,--export=illumo_guest_close -Wl,--export=illumo_guest_shutdown)
endfunction()

illumo_add_guest(PaddleGuest Examples/Paddle/Game.cpp)
illumo_add_guest(PaddlePaletteMod Examples/Paddle/PaletteMod.cpp)
illumo_add_guest(PaddleFaultyMod Examples/Paddle/PaletteMod.cpp)
target_compile_definitions(PaddleFaultyMod PRIVATE ILLUMO_FAULTY_MOD)

add_library(IllumoGuestRendering STATIC
  Source/RecordingBackend.cpp Source/FontProvider.cpp Source/Diagnostics.cpp Source/InputProvider.cpp Source/Files.cpp Source/Environment.cpp Source/Display.cpp Source/Clipboard.cpp Source/Console.cpp Source/Dialog.cpp
  ../Illumo/Source/Rendering/Renderer.cpp
  ../Illumo/Source/Rendering/RendererStyles.cpp
  ../Illumo/Source/Rendering/Camera.cpp
  ../Illumo/Source/Rendering/FontLayout.cpp
  ../Illumo/Source/Rendering/Primitives/GameVisual.cpp
  ../Illumo/Source/Services/EnvVarsValues.cpp)
target_sources(IllumoGuestRendering PRIVATE ../Illumo/Source/Services/InputManagerState.cpp)
target_include_directories(IllumoGuestRendering SYSTEM PRIVATE "${ILLUMO_ROOT}/Illumo/thirdparty/json/single_include")
target_link_libraries(IllumoGuestRendering PUBLIC IllumoGuestOptions)
target_include_directories(IllumoGuestRendering PUBLIC "${ILLUMO_ROOT}/Illumo/Include")
target_include_directories(IllumoGuestRendering SYSTEM PUBLIC "${_illumo_guest_math}")
illumo_add_guest(PresentationGuest Examples/Presentation/Game.cpp)
illumo_add_guest(JobControlGuest Examples/Presentation/JobControl.cpp)
illumo_add_guest(SdkContractGuest Tests/SdkContract.cpp)
target_link_libraries(SdkContractGuest PRIVATE IllumoGuestRendering)
illumo_add_guest(FileControlGuest Tests/FileControl.cpp)
target_link_libraries(FileControlGuest PRIVATE IllumoGuestRendering)
target_link_libraries(PresentationGuest PRIVATE IllumoGuestRendering)

# Shared product algorithms; only the execution scheduler differs from native.
# This target is a migration/parity target, not the shipping game entry point.
add_library(CSimGuestDomain STATIC
  ../IllumoGame/Source/Game/SparseCellGrid.cpp
  ../IllumoGame/Source/Game/SparseWorkerPoolSerial.cpp
  ../IllumoGame/Source/Game/CellPattern.cpp
  ../IllumoGame/Source/Game/PatternCodec.cpp
  ../IllumoGame/Source/Game/BuiltinPatterns.cpp
  ../IllumoGame/Source/Game/IllumoCodecStreams.cpp
  ../IllumoGame/Source/Rulesets/RuleSet.cpp
  ../IllumoGame/Source/Rulesets/DataRuleSet.cpp
  ../IllumoGame/Source/Rulesets/RuleSetRegistry.cpp)
target_include_directories(CSimGuestDomain PUBLIC "${ILLUMO_ROOT}/IllumoGame/Source")
target_include_directories(CSimGuestDomain SYSTEM PRIVATE
  "${ILLUMO_ROOT}/Illumo/thirdparty/json/single_include"
  "${ILLUMO_ROOT}/Illumo/thirdparty/tracy-0.13.1/public")
target_link_libraries(CSimGuestDomain PUBLIC IllumoGuestOptions)

# Guest-side engine: a guest-local IllumoContext (window, input, settings,
# camera, renderer, scene, console bridge, module host) for IModule products.
# Engine UI and scene sources compile unchanged against the recording backend.
add_library(IllumoGuestEngine STATIC
  Source/ModuleApplication.cpp
  Source/PackageAssets.cpp
  Source/Documents.cpp
  ../Illumo/Source/Gui/GridAtlas.cpp
  ../Illumo/Source/Rendering/AssetManager.cpp
  ../Illumo/Source/Rendering/MeshLoader.cpp
  ../Illumo/Source/Services/CommandLineCore.cpp
  ../Illumo/Source/Services/CommandLine.cpp
  ../Illumo/Source/Rendering/GLString.cpp
  ../Illumo/Source/Rendering/SplashText.cpp
  ../Illumo/Source/Rendering/Primitives/MeshVisual.cpp
  ../Illumo/Source/Gui/GuiKit.cpp
  ../Illumo/Source/Gui/GuiMenuShell.cpp
  ../Illumo/Source/Gui/GuiDialog.cpp
  ../Illumo/Source/Scene/SceneGraph.cpp
  ../Illumo/Source/Scene/SceneGraphCompile.cpp
  ../Illumo/Source/Scene/SceneGraphQueries.cpp
  ../Illumo/Source/Scene/SceneQueryIndex.cpp
  ../Illumo/Source/Scene/SceneGraphDrawable.cpp)
target_include_directories(IllumoGuestEngine SYSTEM PRIVATE
  "${ILLUMO_ROOT}/Illumo/thirdparty/json/single_include"
  "${ILLUMO_ROOT}/Illumo/thirdparty/tracy-0.13.1/public"
  "${ILLUMO_ROOT}/Illumo"
  "${ILLUMO_ROOT}/Illumo/thirdparty")
target_link_libraries(IllumoGuestEngine PUBLIC IllumoGuestRendering)

# IllumoGame itself: the complete product as a WASM package module. Native
# IllumoGame sources are shared with the native test oracle; only the
# platform adapter and serial runner are guest-specific.
illumo_add_guest(IllumoGame
  ../IllumoGame/Source/Wasm/GameApplication.cpp
  ../IllumoGame/Source/Wasm/GuestPlatform.cpp
  ../IllumoGame/Source/Wasm/CatalogBootstrap.cpp
  ../IllumoGame/Source/Game/CanvasView.cpp
  ../IllumoGame/Source/Game/CellClipboard.cpp
  ../IllumoGame/Source/Game/CellContext.cpp
  ../IllumoGame/Source/Game/CellGameModule.cpp
  ../IllumoGame/Source/Game/ConfigurationMenu.cpp
  ../IllumoGame/Source/Game/Cursor.cpp
  ../IllumoGame/Source/Game/ExitConfirmDialog.cpp
  ../IllumoGame/Source/Game/IllumoGameConfig.cpp
  ../IllumoGame/Source/Game/MainMenuModule.cpp
  ../IllumoGame/Source/Game/NewSimulationMenu.cpp
  ../IllumoGame/Source/Game/RuleCatalogOverlay.cpp
  ../IllumoGame/Source/Game/RulesetWorkshopMenu.cpp
  ../IllumoGame/Source/Game/SimulationRunnerGeneration.cpp
  ../IllumoGame/Source/Game/SimulationRunnerSerial.cpp)
target_include_directories(IllumoGame SYSTEM PRIVATE
  "${ILLUMO_ROOT}/Illumo/thirdparty/json/single_include"
  "${ILLUMO_ROOT}/Illumo/thirdparty/tracy-0.13.1/public")
target_link_libraries(IllumoGame PRIVATE CSimGuestDomain IllumoGuestEngine)

add_executable(CSimWorkerGuest
  ../IllumoGame/Source/Wasm/SimulationProtocol.cpp
  ../IllumoGame/Source/Wasm/SimulationWorkerExports.cpp)
set_target_properties(CSimWorkerGuest PROPERTIES SUFFIX ".wasm")
target_link_libraries(CSimWorkerGuest PRIVATE CSimGuestDomain)
target_link_options(CSimWorkerGuest PRIVATE
  -Wl,--export=illumo_guest_describe -Wl,--export=illumo_guest_alloc
  -Wl,--export=illumo_guest_free -Wl,--export=illumo_guest_job
  -Wl,--export=illumo_guest_result_size)

add_executable(CSimDomainGuest ../IllumoGame/Tests/Wasm/DomainGuest.cpp)
set_target_properties(CSimDomainGuest PROPERTIES SUFFIX ".wasm")
target_link_libraries(CSimDomainGuest PRIVATE CSimGuestDomain)
target_link_options(CSimDomainGuest PRIVATE
  -Wl,--export=domainInitialize -Wl,--export=domainCase
  -Wl,--export=domainAdvance -Wl,--export=domainHash
  -Wl,--export=domainSave -Wl,--export=domainSaveSize -Wl,--export=domainRestore
  -Wl,--export=allocate -Wl,--export=release)

# IllEd: the world editor as a WASM package module. Sources are shared with
# the native IllEdCore test oracle; only the platform adapter is guest code.
illumo_add_guest(IllEd
  ../IllEd/Source/Wasm/EditorApplication.cpp
  ../IllEd/Source/EditorAttachment.cpp
  ../IllEd/Source/EditorConfirmDialog.cpp
  ../IllEd/Source/EditorDocument.cpp
  ../IllEd/Source/EditorModule.cpp
  ../IllEd/Source/EditorSceneGraphView.cpp
  ../IllEd/Source/EditorSidebar.cpp
  ../IllEd/Source/EditorToolbar.cpp
  ../IllEd/Source/EditorUiAtlas.cpp
  ../IllEd/Source/IllEdConfig.cpp
  ../IllEd/Source/IlscCodec.cpp)
target_include_directories(IllEd PRIVATE "${ILLUMO_ROOT}/IllEd/Source")
target_include_directories(IllEd SYSTEM PRIVATE
  "${ILLUMO_ROOT}/Illumo/thirdparty/json/single_include"
  "${ILLUMO_ROOT}/Illumo/thirdparty/tracy-0.13.1/public")
target_link_libraries(IllEd PRIVATE IllumoGuestEngine)

# IllMeshViewer: the mesh viewer as a WASM package module, sharing its
# sources with the native IllMeshViewerCore test oracle.
illumo_add_guest(IllMeshViewer
  ../IllMeshViewer/Source/Wasm/ViewerApplication.cpp
  ../IllMeshViewer/Source/MeshViewerCamera.cpp
  ../IllMeshViewer/Source/MeshViewerConfig.cpp
  ../IllMeshViewer/Source/MeshViewerModule.cpp
  ../IllMeshViewer/Source/MeshViewerUi.cpp
  ../Illumo/Source/Rendering/Primitives/SkyboxVisual.cpp)
target_include_directories(IllMeshViewer PRIVATE
  "${ILLUMO_ROOT}/IllMeshViewer/Source")
target_include_directories(IllMeshViewer SYSTEM PRIVATE
  "${ILLUMO_ROOT}/Illumo/thirdparty/tracy-0.13.1/public")
target_link_libraries(IllMeshViewer PRIVATE IllumoGuestEngine)
