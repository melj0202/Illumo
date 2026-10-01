foreach(required_value
    TEST_BINARY_MANIFEST BINARY_DIR CTEST_COMMAND
    LLVM_PROFDATA LLVM_COV)
  if(NOT DEFINED ${required_value})
    message(FATAL_ERROR "Coverage value ${required_value} was not supplied")
  endif()
endforeach()
if(NOT EXISTS "${TEST_BINARY_MANIFEST}")
  message(FATAL_ERROR "Workspace coverage binary manifest is missing")
endif()
file(STRINGS "${TEST_BINARY_MANIFEST}" TEST_BINARIES)
if(NOT TEST_BINARIES)
  message(FATAL_ERROR "Workspace coverage binary manifest is empty")
endif()
set(COVERAGE_OBJECTS)
foreach(test_binary IN LISTS TEST_BINARIES)
  if(NOT EXISTS "${test_binary}")
    message(FATAL_ERROR "Workspace coverage binary is missing: ${test_binary}")
  endif()
  if(COVERAGE_OBJECTS)
    list(APPEND COVERAGE_OBJECTS -object)
  endif()
  list(APPEND COVERAGE_OBJECTS "${test_binary}")
  message(STATUS "Coverage binary: ${test_binary}")
endforeach()
if(NOT DEFINED MINIMUM_LINE_COVERAGE)
  set(MINIMUM_LINE_COVERAGE 85)
endif()

file(TO_CMAKE_PATH "${BINARY_DIR}" NORMALIZED_BINARY_DIR)
set(PROFILE_DIR "${NORMALIZED_BINARY_DIR}/coverage-profiles")
set(REPORT_DIR "${NORMALIZED_BINARY_DIR}/coverage-html")
file(REMOVE_RECURSE "${PROFILE_DIR}" "${REPORT_DIR}")
file(MAKE_DIRECTORY "${PROFILE_DIR}" "${REPORT_DIR}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    # Process-isolated CTest cases can reuse Windows PIDs. LLVM's merge pool
    # keeps profiles distinct per instrumented binary and merges repeated runs.
    "LLVM_PROFILE_FILE=${PROFILE_DIR}/IllumoWorkspace-%4m.profraw"
    "${CTEST_COMMAND}" --test-dir "${BINARY_DIR}" -C "${CONFIG}"
    -L IllumoWorkspace -j 4 --output-on-failure
  RESULT_VARIABLE TEST_RESULT)
if(NOT TEST_RESULT EQUAL 0)
  message(FATAL_ERROR "Illumo workspace coverage tests failed")
endif()

file(GLOB PROFILE_FILES "${PROFILE_DIR}/IllumoWorkspace-*.profraw")
if(NOT PROFILE_FILES)
  message(FATAL_ERROR "No LLVM raw profiles were produced")
endif()
set(PROFILE_DATA "${NORMALIZED_BINARY_DIR}/IllumoWorkspace.profdata")
execute_process(
  COMMAND "${LLVM_PROFDATA}" merge -sparse ${PROFILE_FILES}
    -o "${PROFILE_DATA}"
  RESULT_VARIABLE MERGE_RESULT
  ERROR_VARIABLE MERGE_ERROR)
if(NOT MERGE_RESULT EQUAL 0)
  message(FATAL_ERROR "llvm-profdata failed: ${MERGE_ERROR}")
endif()

set(IGNORE_REGEX
  [=[([/\\]Tests[/\\]|[/\\]TestSupport[/\\]|thirdparty|Program Files|scoop|Microsoft Visual Studio|Windows Kits|Rendering[/\\]OpenGL|Rendering[/\\]RenderWindow[.])]=])
execute_process(
  COMMAND "${LLVM_COV}" report ${COVERAGE_OBJECTS}
    "--instr-profile=${PROFILE_DATA}"
    "--ignore-filename-regex=${IGNORE_REGEX}"
  RESULT_VARIABLE REPORT_RESULT
  OUTPUT_VARIABLE COVERAGE_REPORT
  ERROR_VARIABLE REPORT_ERROR)
if(NOT REPORT_RESULT EQUAL 0)
  message(FATAL_ERROR "llvm-cov report failed: ${REPORT_ERROR}")
endif()
if(NOT REPORT_ERROR STREQUAL "")
  message("${REPORT_ERROR}")
endif()

message("${COVERAGE_REPORT}")
string(REGEX MATCH "TOTAL[^\r\n]*" TOTAL_LINE "${COVERAGE_REPORT}")
if(TOTAL_LINE STREQUAL "")
  message(FATAL_ERROR "Unable to find TOTAL in llvm-cov report")
endif()
string(REGEX REPLACE "[ \t]+" ";" TOTAL_FIELDS "${TOTAL_LINE}")
list(LENGTH TOTAL_FIELDS TOTAL_FIELD_COUNT)
if(TOTAL_FIELD_COUNT LESS 10)
  message(FATAL_ERROR "Unable to parse llvm-cov TOTAL line: ${TOTAL_LINE}")
endif()
list(GET TOTAL_FIELDS 9 LINE_COVERAGE_WITH_PERCENT)
string(REPLACE "%" "" LINE_COVERAGE "${LINE_COVERAGE_WITH_PERCENT}")
execute_process(
  COMMAND "${LLVM_COV}" show ${COVERAGE_OBJECTS}
    "--instr-profile=${PROFILE_DATA}"
    "--ignore-filename-regex=${IGNORE_REGEX}"
    -format=html "-output-dir=${REPORT_DIR}"
  RESULT_VARIABLE HTML_RESULT
  ERROR_VARIABLE HTML_ERROR)
if(NOT HTML_RESULT EQUAL 0)
  message(FATAL_ERROR "llvm-cov HTML report failed: ${HTML_ERROR}")
endif()

message(STATUS "Illumo workspace production line coverage: ${LINE_COVERAGE}%")
message(STATUS "HTML report: ${REPORT_DIR}/index.html")
if(LINE_COVERAGE LESS MINIMUM_LINE_COVERAGE)
  message(FATAL_ERROR
    "Production line coverage ${LINE_COVERAGE}% is below ${MINIMUM_LINE_COVERAGE}%")
endif()
message(STATUS "Coverage gate passed: ${MINIMUM_LINE_COVERAGE}% minimum")
