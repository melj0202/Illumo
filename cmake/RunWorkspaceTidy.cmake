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

execute_process(
  COMMAND
    "${PYTHON}"
    "${CMAKE_CURRENT_LIST_DIR}/RunWorkspaceTidy.py"
    --source-dir "${SOURCE_DIR}"
    --binary-dir "${BINARY_DIR}"
    --clang-tidy "${CLANG_TIDY}"
    --run-clang-tidy "${RUN_CLANG_TIDY}"
    --python "${PYTHON}"
    --jobs "${JOBS}"
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE TIDY_RESULT)
if(NOT TIDY_RESULT EQUAL 0)
  message(FATAL_ERROR
    "clang-tidy reported diagnostics in first-party sources (exit ${TIDY_RESULT})")
endif()
