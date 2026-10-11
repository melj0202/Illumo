cmake_minimum_required(VERSION 3.25)

project(IllumoWorkspace LANGUAGES C CXX)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/IllumoBuild.cmake")
include(CTest)

set(ILLUMO_WORKSPACE_BUILD ON)
add_subdirectory(Illumo)

# The game is an optional nested repository; the engine builds without it.
# A plain set(), not a cache entry, so a stale cache cannot keep an old folder.
set(ILLUMO_GAME_DIR "CSim")
set(ILLUMO_PROGRAMS IllEd IllMeshViewer Playground)
set(_illumo_test_runners IllumoTestsDiscover IllEdTestsDiscover
  IllMeshViewerTestsDiscover IllumoPublicHeaderSmoke)
set(ILLUMO_HAS_GAME OFF)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${ILLUMO_GAME_DIR}/CMakeLists.txt")
  set(ILLUMO_HAS_GAME ON)
  add_subdirectory("${ILLUMO_GAME_DIR}")
  list(APPEND ILLUMO_PROGRAMS "${ILLUMO_GAME_DIR}")
  list(APPEND _illumo_test_runners IllumoGameTestsDiscover)
endif()

add_subdirectory(IllEd)
add_subdirectory(IllMeshViewer)

if(BUILD_TESTING)
  # The workspace builds without exceptions; see cmake/CheckNoExceptions.cmake.
  add_test(NAME Illumo.Build.NoExceptions
    COMMAND ${CMAKE_COMMAND} "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
      "-DGAME_DIR=${ILLUMO_GAME_DIR}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckNoExceptions.cmake")
  set_tests_properties(Illumo.Build.NoExceptions PROPERTIES
    LABELS "Illumo;IllumoWorkspace")
  add_custom_target(IllumoRunTests ${ILLUMO_RUN_TESTS_ALL}
    COMMAND ${CMAKE_CTEST_COMMAND} -C $<CONFIG>
      -L IllumoWorkspace --output-on-failure
    DEPENDS ${_illumo_test_runners}
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    USES_TERMINAL
    COMMENT "Running the Illumo workspace test suite"
  )
endif()

# The WASM programs (ILLUMO_PROGRAMS, above): each directory has a
# GuestTargets.cmake and a PackageTargets.cmake (cmake/IllumoWasm.cmake).
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/IllumoWasm.cmake")

if(ILLUMO_ENABLE_COVERAGE)
  illumo_add_workspace_coverage()
endif()

if(ILLUMO_ENABLE_CLANG_TIDY)
  find_package(Python3 COMPONENTS Interpreter)
  get_filename_component(_illumo_clang_tidy_dir
    "${ILLUMO_CLANG_TIDY_EXECUTABLE}" DIRECTORY)
  find_program(ILLUMO_RUN_CLANG_TIDY_EXECUTABLE
    NAMES run-clang-tidy run-clang-tidy.py
    HINTS "${_illumo_clang_tidy_dir}")
  unset(_illumo_clang_tidy_dir)
  if(Python3_Interpreter_FOUND AND ILLUMO_RUN_CLANG_TIDY_EXECUTABLE)
    add_custom_target(IllumoTidy
      COMMAND ${CMAKE_COMMAND}
        "-DBINARY_DIR=${CMAKE_BINARY_DIR}"
        "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
        "-DCLANG_TIDY=${ILLUMO_CLANG_TIDY_EXECUTABLE}"
        "-DRUN_CLANG_TIDY=${ILLUMO_RUN_CLANG_TIDY_EXECUTABLE}"
        "-DPYTHON=${Python3_EXECUTABLE}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunWorkspaceTidy.cmake"
      USES_TERMINAL
      COMMENT "Running clang-tidy on first-party workspace sources"
    )
  else()
    message(STATUS
      "IllumoTidy skipped: Python3 and run-clang-tidy are required")
  endif()
endif()
