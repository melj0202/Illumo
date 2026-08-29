cmake_minimum_required(VERSION 3.20)

project(IllumoWorkspace LANGUAGES C CXX)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/IllumoBuild.cmake")
include(CTest)

set(ILLUMO_WORKSPACE_BUILD ON)
add_subdirectory(Illumo)
add_subdirectory(IllumoGame)
add_subdirectory(IllEd)

if(BUILD_TESTING)
  add_custom_target(IllumoRunTests ALL
    COMMAND ${CMAKE_CTEST_COMMAND} -C $<CONFIG>
      -L IllumoWorkspace --output-on-failure
    DEPENDS IllumoTestsDiscover IllumoGameTestsDiscover
      IllEdTestsDiscover IllumoPublicHeaderSmoke
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
