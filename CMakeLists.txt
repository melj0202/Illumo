cmake_minimum_required(VERSION 3.20)

project(IllumoWorkspace LANGUAGES C CXX)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/IllumoBuild.cmake")
include(CTest)

set(ILLUMO_WORKSPACE_BUILD ON)
add_subdirectory(Illumo)
add_subdirectory(IllumoGame)
add_subdirectory(IllEd)
add_subdirectory(IllMeshViewer)

if(BUILD_TESTING)
  add_custom_target(IllumoRunTests ALL
    COMMAND ${CMAKE_CTEST_COMMAND} -C $<CONFIG>
      -L IllumoWorkspace --output-on-failure
    DEPENDS IllumoTestsDiscover IllumoGameTestsDiscover
      IllEdTestsDiscover IllMeshViewerTestsDiscover IllumoPublicHeaderSmoke
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    USES_TERMINAL
    COMMENT "Running the Illumo workspace test suite"
  )
endif()

if(ILLUMO_ENABLE_COVERAGE)
  find_program(ILLUMO_LLVM_PROFDATA_EXECUTABLE NAMES llvm-profdata REQUIRED)
  find_program(ILLUMO_LLVM_COV_EXECUTABLE NAMES llvm-cov REQUIRED)
  add_custom_target(IllumoCoverage
    COMMAND ${CMAKE_COMMAND}
      "-DTEST_BINARY_ILLUMO=$<TARGET_FILE:IllumoTests>"
      "-DTEST_BINARY_GAME=$<TARGET_FILE:IllumoGameTests>"
      "-DBINARY_DIR=${CMAKE_BINARY_DIR}"
      "-DCTEST_COMMAND=${CMAKE_CTEST_COMMAND}"
      "-DCONFIG=$<CONFIG>"
      "-DLLVM_PROFDATA=${ILLUMO_LLVM_PROFDATA_EXECUTABLE}"
      "-DLLVM_COV=${ILLUMO_LLVM_COV_EXECUTABLE}"
      "-DMINIMUM_LINE_COVERAGE=85"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunWorkspaceCoverage.cmake"
    DEPENDS IllumoTestsDiscover IllumoGameTestsDiscover
      IllumoPublicHeaderSmoke
    USES_TERMINAL
    COMMENT "Running combined Illumo workspace coverage"
  )
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
