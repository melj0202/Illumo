cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(Illumo LANGUAGES C CXX)
  include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/IllumoBuild.cmake")
  include(CTest)
  if(ILLUMO_ENABLE_COVERAGE)
    message(FATAL_ERROR
      "Combined coverage is a workspace-only target; configure from the repository root")
  endif()
endif()

set(ILLUMO_LIBRARY_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
  CACHE INTERNAL "Illumo library source directory")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/IllumoRuntime.cmake")

find_package(OpenGL REQUIRED)

set(ILLUMO_PLATFORM_ENTRY_SOURCE)
set(ILLUMO_PLATFORM_SOURCES)
set(ILLUMO_PLATFORM_LIBRARIES)
if(WIN32)
  set(ILLUMO_PLATFORM_ENTRY_SOURCE Source/Platform/Windows/WinMain.cpp)
  list(APPEND ILLUMO_PLATFORM_SOURCES
    Source/Platform/Windows/WinSaveLoad.cpp
    Source/Platform/Windows/WinClipboard.cpp)
  list(APPEND ILLUMO_PLATFORM_LIBRARIES Comdlg32)
elseif(APPLE)
  enable_language(OBJCXX)
  set(ILLUMO_PLATFORM_ENTRY_SOURCE Source/Platform/macOS/Main.cpp)
  list(APPEND ILLUMO_PLATFORM_SOURCES
    Source/Platform/macOS/MacSaveLoad.mm
    Source/Platform/macOS/MacClipboard.cpp)
  list(APPEND ILLUMO_PLATFORM_LIBRARIES
    "-framework Cocoa" "-framework OpenGL" "-framework IOKit"
    "-framework CoreVideo")
elseif(UNIX)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GTK REQUIRED gtkmm-3.0)
  set(ILLUMO_PLATFORM_ENTRY_SOURCE Source/Platform/Linux/_main.cpp)
  list(APPEND ILLUMO_PLATFORM_SOURCES
    Source/Platform/Linux/LinuxSaveLoad.cpp
    Source/Platform/Linux/LinuxClipboard.cpp)
  list(APPEND ILLUMO_PLATFORM_LIBRARIES ${GTK_LIBRARIES})
else()
  message(FATAL_ERROR "Illumo has no platform entry implementation")
endif()

add_subdirectory("thirdparty/glfw-3.4" SYSTEM)
add_subdirectory("thirdparty/glm" SYSTEM)
add_subdirectory("thirdparty/freetype-2.13.3" SYSTEM)

if(NOT TARGET glew_s)
  add_library(glew_s STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/glew-2.1.0/src/glew.c")
  target_include_directories(glew_s PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/glew-2.1.0/include")
  target_compile_definitions(glew_s PUBLIC GLEW_STATIC)
  target_compile_definitions(glew_s PRIVATE GLEW_NO_GLU)
  if(MSVC)
    target_compile_definitions(glew_s PRIVATE VC_EXTRALEAN)
    target_compile_options(glew_s PRIVATE /GS-)
  endif()
  target_link_libraries(glew_s PUBLIC OpenGL::GL)
  set_target_properties(glew_s PROPERTIES
    OUTPUT_NAME "libglew32"
    FOLDER "thirdparty")
endif()

add_library(Illumo STATIC
  Source/Foundation/MacroDefs.cpp
  Source/Scene/SceneGraph.cpp
  Source/Services/CommandLine.cpp
  Source/Services/DebugAlloc.cpp
  Source/Services/EnvVars.cpp
  Source/Services/InputManager.cpp
  Source/Services/Logger.cpp
  Source/Services/PoolAlloc.cpp
  Source/Services/SysCmdLine.cpp
  Source/Rendering/AssetManager.cpp
  Source/Rendering/Camera.cpp
  Source/Rendering/GLString.cpp
  Source/Rendering/RendererStyles.cpp
  Source/Rendering/RenderWindow.cpp
  Source/Rendering/SplashText.cpp
  Source/Rendering/Primitives/MeshVisual.cpp
  Source/Rendering/Primitives/GameVisual.cpp
  Source/Rendering/Primitives/SpriteAnimation.cpp
  Source/Rendering/OpenGL/CreateOpenGLBackend.cpp
  Source/Rendering/OpenGL/GLBackend.cpp
  Source/Rendering/OpenGL/GLDevice.cpp
  Source/Engine/Illumo.cpp
  Source/Engine/Application.cpp
  ${ILLUMO_PLATFORM_SOURCES}
  thirdparty/tracy-0.13.1/public/TracyClient.cpp
)
target_sources(Illumo PRIVATE
  $<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/Source/Engine/DebugModule.cpp>)
add_library(Illumo::Illumo ALIAS Illumo)

target_include_directories(Illumo
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include>"
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/tracy-0.13.1/public"
)
target_include_directories(Illumo SYSTEM PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty>")
target_include_directories(Illumo SYSTEM PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/glfw-3.4/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/glew-2.1.0/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/json/single_include"
  "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty"
)
target_link_libraries(Illumo PRIVATE
  glm::glm glew_s glfw OpenGL::GL freetype-interface freetype
  ${ILLUMO_PLATFORM_LIBRARIES})
illumo_configure_runtime_target(Illumo)

if(UNIX AND NOT APPLE)
  target_compile_options(Illumo PRIVATE ${GTK_CFLAGS_OTHER})
  target_include_directories(Illumo SYSTEM PRIVATE ${GTK_INCLUDE_DIRS})
  target_link_directories(Illumo PRIVATE ${GTK_LIBRARY_DIRS})
endif()

add_library(IllumoPlatformEntry OBJECT ${ILLUMO_PLATFORM_ENTRY_SOURCE})
add_library(Illumo::PlatformEntry ALIAS IllumoPlatformEntry)
target_link_libraries(IllumoPlatformEntry PRIVATE Illumo::Illumo)
illumo_configure_runtime_target(IllumoPlatformEntry)

add_library(IllumoTestSupport INTERFACE)
add_library(Illumo::TestSupport ALIAS IllumoTestSupport)
target_link_libraries(IllumoTestSupport INTERFACE Illumo::Illumo)
target_include_directories(IllumoTestSupport INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/TestSupport/Include>")

if(BUILD_TESTING)
  add_executable(IllumoTests
    Tests/TestMain.cpp
    Tests/TestMockBackend.cpp
    Tests/TestRendererE2E.cpp
    Tests/TestUITokens.cpp
    Tests/TestServices.cpp
    Tests/TestRuntimeUtilities.cpp
    Tests/TestAllocators.cpp
    Tests/TestGameVisual.cpp
    Tests/TestMeshVisual.cpp
    Tests/TestIllumoHost.cpp
    Tests/TestTextureUploadPolicy.cpp
    Tests/TestSysCmdLine.cpp
    Tests/TestSceneGraph.cpp
  )
  target_link_libraries(IllumoTests PRIVATE Illumo::TestSupport glfw)
  target_include_directories(IllumoTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source")
  target_compile_definitions(IllumoTests PRIVATE ILLUMO_TESTING)
  illumo_configure_runtime_target(IllumoTests)
  illumo_discover_test_runner(IllumoTests Illumo)
  illumo_stage_msvc_asan(IllumoTests)
  illumo_stage_shaders(IllumoTests "${CMAKE_CURRENT_SOURCE_DIR}/Shader")

  add_executable(IllumoPublicHeaderSmoke Tests/PublicHeaderSmoke.cpp)
  target_link_libraries(IllumoPublicHeaderSmoke PRIVATE Illumo::Illumo)
  illumo_configure_runtime_target(IllumoPublicHeaderSmoke)
  illumo_stage_msvc_asan(IllumoPublicHeaderSmoke)
  add_test(NAME Illumo.PublicHeaders.ConsumerSmoke
    COMMAND IllumoPublicHeaderSmoke)
  set_tests_properties(Illumo.PublicHeaders.ConsumerSmoke PROPERTIES
    LABELS "Illumo;IllumoWorkspace")

  if(NOT ILLUMO_WORKSPACE_BUILD)
    add_custom_target(IllumoRunTests ALL
      COMMAND ${CMAKE_CTEST_COMMAND} -C $<CONFIG>
        -L Illumo --output-on-failure
      DEPENDS IllumoTestsDiscover IllumoPublicHeaderSmoke
      WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
      USES_TERMINAL
      COMMENT "Running the standalone Illumo test suite")
  endif()
endif()

option(ILLUMO_BUILD_DOCUMENTATION
  "Build the Illumo PDFs with the default build" ON)
if(ILLUMO_BUILD_DOCUMENTATION AND NOT TARGET IllumoDocs)
  find_program(ILLUMO_POWERSHELL_EXECUTABLE NAMES powershell pwsh)
  find_program(ILLUMO_LATEXMK_EXECUTABLE NAMES latexmk)
  if(WIN32 AND ILLUMO_POWERSHELL_EXECUTABLE AND ILLUMO_LATEXMK_EXECUTABLE)
    add_custom_target(IllumoDocs ALL
      COMMAND "${ILLUMO_POWERSHELL_EXECUTABLE}"
        -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/../docs/build.ps1"
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../docs"
      USES_TERMINAL
      COMMENT "Building Illumo documentation")
  else()
    message(STATUS "IllumoDocs skipped: PowerShell and latexmk are required")
  endif()
endif()
