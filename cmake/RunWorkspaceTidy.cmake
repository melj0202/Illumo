foreach(required_value BINARY_DIR SOURCE_DIR CLANG_TIDY PYTHON RUN_CLANG_TIDY)
  if(NOT DEFINED ${required_value} OR "${${required_value}}" STREQUAL "")
    message(FATAL_ERROR "Tidy value ${required_value} was not supplied")
  endif()
endforeach()

set(COMPILE_COMMANDS "${BINARY_DIR}/compile_commands.json")
if(NOT EXISTS "${COMPILE_COMMANDS}")
  message(FATAL_ERROR
    "clang-tidy requires compile_commands.json under ${BINARY_DIR}. "
    "Configure the workspace with Ninja or another Makefile-like generator.")
endif()

if(NOT DEFINED JOBS OR JOBS STREQUAL "" OR JOBS STREQUAL "0")
  include(ProcessorCount)
  ProcessorCount(JOBS)
  if(JOBS EQUAL 0)
    set(JOBS 1)
  endif()
endif()

set(FILE_REGEX
  "(IllumoGame|IllMeshViewer|IllEd|Illumo)[/\\\\](Include|Source|Tests|TestSupport)[/\\\\]")

file(STRINGS "${COMPILE_COMMANDS}" COMPILE_FILE_LINES REGEX "\"file\"")
set(MATCHED_TRANSLATION_UNITS 0)
foreach(line IN LISTS COMPILE_FILE_LINES)
  if(line MATCHES "thirdparty")
    continue()
  endif()
  if(line MATCHES "${FILE_REGEX}")
    math(EXPR MATCHED_TRANSLATION_UNITS "${MATCHED_TRANSLATION_UNITS} + 1")
  endif()
endforeach()
if(MATCHED_TRANSLATION_UNITS EQUAL 0)
  message(FATAL_ERROR
    "No first-party translation units matched ${FILE_REGEX} in "
    "${COMPILE_COMMANDS}")
endif()

message(STATUS
  "Running clang-tidy on ${MATCHED_TRANSLATION_UNITS} first-party "
  "translation units (${JOBS} jobs)")

execute_process(
  COMMAND
    "${PYTHON}"
    "${RUN_CLANG_TIDY}"
    -p "${BINARY_DIR}"
    -clang-tidy-binary "${CLANG_TIDY}"
    -config-file "${SOURCE_DIR}/.clang-tidy"
    -j "${JOBS}"
    -quiet
    -hide-progress
    --
    "${FILE_REGEX}"
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE TIDY_RESULT)
if(NOT TIDY_RESULT EQUAL 0)
  message(FATAL_ERROR
    "clang-tidy reported diagnostics in first-party sources (exit ${TIDY_RESULT})")
endif()
