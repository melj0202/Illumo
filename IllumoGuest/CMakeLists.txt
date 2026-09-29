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
target_include_directories(IllumoGuestOptions INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/Include"
  "${ILLUMO_ROOT}/Illumo/Include")
# ILLUMO_ENABLE_TRACY builds: <Illumo/Foundation/Profile.h> markers call the
# host's illumo_profile imports, which report to the runtime's Tracy client.
option(ILLUMO_GUEST_PROFILE "Report guest profiling markers to the host" OFF)
if(ILLUMO_GUEST_PROFILE)
  target_compile_definitions(IllumoGuestOptions INTERFACE ILLUMO_GUEST_PROFILE)
endif()

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
  Source/RecordingBackend.cpp Source/RenderWorld.cpp Source/VisualProxies.cpp Source/FontProvider.cpp Source/Diagnostics.cpp Source/InputProvider.cpp Source/Files.cpp Source/FileTree.cpp Source/Environment.cpp Source/Display.cpp Source/Clipboard.cpp Source/Console.cpp Source/Dialog.cpp Source/Audio.cpp
  ../Illumo/Source/Audio/AudioDecoder.cpp
  ../Illumo/Source/Rendering/Renderer.cpp
  ../Illumo/Source/Rendering/RendererStyles.cpp
  ../Illumo/Source/Rendering/Camera.cpp
  ../Illumo/Source/Rendering/FontLayout.cpp
  ../Illumo/Source/Rendering/FontWeightRamp.cpp
  ../Illumo/Source/Rendering/Primitives/GameVisual.cpp
  ../Illumo/Source/Rendering/Primitives/SkyboxVisual.cpp
  ../Illumo/Source/Services/EnvVarsValues.cpp)
target_sources(IllumoGuestRendering PRIVATE ../Illumo/Source/Services/InputManagerState.cpp)
target_include_directories(IllumoGuestRendering SYSTEM PRIVATE
  "${ILLUMO_ROOT}/Illumo/thirdparty/json/single_include"
  "${ILLUMO_ROOT}/Illumo/thirdparty/miniaudio-0.11.25")
# Guests decode their own sound files; miniaudio here is only its decoders
# (no devices, threads, mixing engine or node graph), reached through their
# memory entry points so no file-system import is linked. The host mixes.
target_compile_definitions(IllumoGuestRendering PRIVATE
  MA_NO_DEVICE_IO MA_NO_THREADING MA_NO_ENGINE MA_NO_NODE_GRAPH
  MA_NO_RESOURCE_MANAGER MA_NO_ENCODING MA_NO_GENERATION)
target_link_libraries(IllumoGuestRendering PUBLIC IllumoGuestOptions)
target_include_directories(IllumoGuestRendering PUBLIC "${ILLUMO_ROOT}/Illumo/Include")
target_include_directories(IllumoGuestRendering SYSTEM PUBLIC "${_illumo_guest_math}")
illumo_add_guest(PresentationGuest Examples/Presentation/Game.cpp)
illumo_add_guest(JobControlGuest Examples/Presentation/JobControl.cpp)
illumo_add_guest(SdkContractGuest Tests/SdkContract.cpp)
# The scene worlds contract needs the content library (SceneDirector).
target_link_libraries(SdkContractGuest PRIVATE IllumoGuestRendering IllumoGuestContent)
illumo_add_guest(FileControlGuest Tests/FileControl.cpp)
target_link_libraries(FileControlGuest PRIVATE IllumoGuestRendering)
target_link_libraries(PresentationGuest PRIVATE IllumoGuestRendering)

# Guest-side engine: the engine UI, scene graph and asset sources a guest-local
# IllumoContext is composed from.
# Engine UI and scene sources compile unchanged against the recording backend.
add_library(IllumoGuestEngine STATIC
  Source/PanelSurfaces.cpp
  Source/GuestAssetSource.cpp
  Source/VfsAssets.cpp
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
  ../Illumo/Source/Gui/GuiDropdownList.cpp
  ../Illumo/Source/Gui/GuiEngineBrand.cpp
  ../Illumo/Source/Gui/GuiFileTree.cpp
  ../Illumo/Source/Gui/GuiPanelDock.cpp
  ../Illumo/Source/Gui/GuiPanelPointer.cpp
  ../Illumo/Source/Gui/GuiToolStyle.cpp
  ../Illumo/Source/Gui/GuiTextEdit.cpp
  ../Illumo/Source/Scene/SceneGraph.cpp
  ../Illumo/Source/Scene/SceneGraphCompile.cpp
  ../Illumo/Source/Scene/SceneGraphQueries.cpp
  ../Illumo/Source/Scene/SceneQueryIndex.cpp
  ../Illumo/Source/Scene/SceneGraphDrawable.cpp)
target_include_directories(IllumoGuestEngine SYSTEM PRIVATE
  "${ILLUMO_ROOT}/Illumo/thirdparty/json/single_include"
  "${ILLUMO_ROOT}/Illumo/thirdparty/tracy-0.14.1/public"
  "${ILLUMO_ROOT}/Illumo"
  "${ILLUMO_ROOT}/Illumo/thirdparty")
target_link_libraries(IllumoGuestEngine PUBLIC IllumoGuestRendering)

# Guest mirror of Illumo::Content: the serial-safe subset (virtual paths,
# package manifests, the .ilsc scene format, program scenes) and GuestProgram,
# which composes the guest-local IllumoContext and runs a product's scenes.
# Archives and the virtual file tree stay host-side; guests reach them through
# file services.
add_library(IllumoGuestContent STATIC
  ../Illumo/Source/Content/IlscCodec.cpp
  ../Illumo/Source/Content/PackageManifest.cpp
  ../Illumo/Source/Content/ProgramScene.cpp
  ../Illumo/Source/Content/SceneAssetRefs.cpp
  ../Illumo/Source/Content/SceneDirector.cpp
  ../Illumo/Source/Content/SceneDocument.cpp
  ../Illumo/Source/Content/SceneInstance.cpp
  ../Illumo/Source/Content/ScenePrimitiveMeshes.cpp
  ../Illumo/Source/Content/VirtualPath.cpp
  Source/Program.cpp
  Source/SceneFetches.cpp
  Source/SceneWorlds.cpp)
target_include_directories(IllumoGuestContent SYSTEM PRIVATE
  "${ILLUMO_ROOT}/Illumo/thirdparty/json/single_include"
  "${ILLUMO_ROOT}/Illumo/thirdparty/tracy-0.14.1/public"
  "${ILLUMO_ROOT}/Illumo"
  "${ILLUMO_ROOT}/Illumo/thirdparty")
target_link_libraries(IllumoGuestContent PUBLIC IllumoGuestEngine)

# The programs this workspace builds (the products here, or a generated
# project's own): each directory's GuestTargets.cmake adds its modules with
# illumo_add_guest. cmake/IllumoWasm.cmake passes their absolute paths.
set(ILLUMO_PROGRAMS "" CACHE STRING "Program directories with a GuestTargets.cmake")
foreach(_program IN LISTS ILLUMO_PROGRAMS)
  include("${_program}/GuestTargets.cmake")
endforeach()
