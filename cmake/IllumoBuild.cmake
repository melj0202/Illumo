include_guard(GLOBAL)

option(ILLUMO_ENABLE_TRACY
  "Enable Tracy instrumentation in optimized builds" OFF)
option(ILLUMO_ENABLE_COVERAGE
  "Instrument first-party workspace targets for LLVM coverage" OFF)
option(ILLUMO_ENABLE_CLANG_TIDY
  "Run clang-tidy on first-party C++ during build" ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

get_filename_component(ILLUMO_WORKSPACE_SOURCE_ROOT
  "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(ILLUMO_ENABLE_CLANG_TIDY)
  find_program(ILLUMO_CLANG_TIDY_EXECUTABLE NAMES clang-tidy)
  if(NOT ILLUMO_CLANG_TIDY_EXECUTABLE)
    message(FATAL_ERROR
      "ILLUMO_ENABLE_CLANG_TIDY=ON requires clang-tidy on PATH. "
      "Install LLVM or configure with -DILLUMO_ENABLE_CLANG_TIDY=OFF.")
  endif()
  set(ILLUMO_CLANG_TIDY_COMMAND
    "${ILLUMO_CLANG_TIDY_EXECUTABLE};--config-file=${ILLUMO_WORKSPACE_SOURCE_ROOT}/.clang-tidy;--quiet"
    CACHE INTERNAL "clang-tidy command attached to first-party C++ targets")
endif()

if(NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
  else()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
  endif()
endif()
if(NOT DEFINED CMAKE_LIBRARY_OUTPUT_DIRECTORY)
  if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
  else()
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
  endif()
endif()
if(NOT DEFINED CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
  if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
  else()
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
  endif()
endif()

if(MSVC)
  add_compile_options(/FS)
endif()

function(illumo_configure_cpp_target target_name)
  target_compile_features(${target_name} PUBLIC cxx_std_23)

  if(MSVC)
    target_compile_options(${target_name} PRIVATE /W4 /FS /MP)
  else()
    target_compile_options(${target_name} PRIVATE -Wall -Wextra)
  endif()

  target_compile_definitions(${target_name} PRIVATE
    $<$<CONFIG:Debug>:TRACY_ENABLE>
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:ILLUMO_ENABLE_DEBUG_TOOLS=1>
  )
  if(ILLUMO_ENABLE_TRACY)
    target_compile_definitions(${target_name} PRIVATE TRACY_ENABLE)
  endif()

  if(ILLUMO_ENABLE_CLANG_TIDY)
    set_target_properties(${target_name} PROPERTIES
      CXX_CLANG_TIDY "${ILLUMO_CLANG_TIDY_COMMAND}")
  endif()

  if(ILLUMO_ENABLE_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      message(FATAL_ERROR "ILLUMO_ENABLE_COVERAGE requires Clang/LLVM")
    endif()
    target_compile_options(${target_name} PRIVATE
      -fprofile-instr-generate
      -fcoverage-mapping
    )
    get_target_property(target_type ${target_name} TYPE)
    if(NOT target_type STREQUAL "STATIC_LIBRARY" AND
       NOT target_type STREQUAL "OBJECT_LIBRARY" AND
       NOT target_type STREQUAL "INTERFACE_LIBRARY")
      target_link_options(${target_name} PRIVATE -fprofile-instr-generate)
    endif()
  endif()
endfunction()

function(illumo_configure_runtime_target target_name)
  illumo_configure_cpp_target(${target_name})
  if(MSVC)
    target_compile_options(${target_name} PRIVATE
      $<$<CONFIG:Debug>:/Od>
      $<$<CONFIG:Release>:/O2>)
    if(NOT ILLUMO_ENABLE_COVERAGE)
      target_compile_options(${target_name} PRIVATE
        $<$<CONFIG:Debug>:/fsanitize=address>)
    endif()
  else()
    target_compile_options(${target_name} PRIVATE
      $<$<CONFIG:Debug>:-O0>
      $<$<CONFIG:Debug>:-g>
      $<$<CONFIG:Release>:-O3>)
    if(NOT ILLUMO_ENABLE_COVERAGE)
      target_compile_options(${target_name} PRIVATE
        $<$<CONFIG:Debug>:-fsanitize=address>)
      get_target_property(target_type ${target_name} TYPE)
      if(NOT target_type STREQUAL "STATIC_LIBRARY" AND
         NOT target_type STREQUAL "OBJECT_LIBRARY" AND
         NOT target_type STREQUAL "INTERFACE_LIBRARY")
        target_link_options(${target_name} PRIVATE
          $<$<CONFIG:Debug>:-fsanitize=address>)
      endif()
    endif()
  endif()
endfunction()

function(illumo_discover_test_runner target_name label_name)
  set_property(GLOBAL APPEND PROPERTY ILLUMO_WORKSPACE_TEST_RUNNERS ${target_name})
  set(discovery_file
    "${CMAKE_CURRENT_BINARY_DIR}/${target_name}-$<CONFIG>-discovered.cmake")
  set(discovery_include
    "${CMAKE_CURRENT_BINARY_DIR}/${target_name}-include.cmake")
  file(TO_CMAKE_PATH "${CMAKE_CURRENT_BINARY_DIR}" discovery_directory)
  file(WRITE "${discovery_include}"
    "set(_illumo_test_config \"\${CTEST_CONFIGURATION_TYPE}\")\n"
    "if(_illumo_test_config STREQUAL \"\")\n"
    "  set(_illumo_test_config \"${CMAKE_BUILD_TYPE}\")\n"
    "endif()\n"
    "set(_illumo_test_file \"${discovery_directory}/${target_name}-\${_illumo_test_config}-discovered.cmake\")\n"
    "if(NOT EXISTS \"\${_illumo_test_file}\")\n"
    "  if(EXISTS \"${discovery_directory}/${target_name}-Release-discovered.cmake\")\n"
    "    set(_illumo_test_file \"${discovery_directory}/${target_name}-Release-discovered.cmake\")\n"
    "  elseif(EXISTS \"${discovery_directory}/${target_name}-Debug-discovered.cmake\")\n"
    "    set(_illumo_test_file \"${discovery_directory}/${target_name}-Debug-discovered.cmake\")\n"
    "  endif()\n"
    "endif()\n"
    "if(EXISTS \"\${_illumo_test_file}\")\n"
    "  include(\"\${_illumo_test_file}\")\n"
    "endif()\n"
    "unset(_illumo_test_file)\n"
    "unset(_illumo_test_config)\n")
  add_custom_command(TARGET ${target_name} POST_BUILD
    COMMAND $<TARGET_FILE:${target_name}>
      --write-ctest
      "${discovery_file}"
      $<TARGET_FILE:${target_name}>
      "${CMAKE_BINARY_DIR}"
    VERBATIM
  )
  add_custom_target(${target_name}Discover
    COMMAND $<TARGET_FILE:${target_name}>
      --write-ctest
      "${discovery_file}"
      $<TARGET_FILE:${target_name}>
      "${CMAKE_BINARY_DIR}"
    DEPENDS ${target_name}
    VERBATIM
    COMMENT "Refreshing ${label_name} CTest discovery")
  set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES
    "${discovery_include}")
endfunction()

# Call after the workspace subdirectories have registered their test runners.
function(illumo_add_workspace_coverage)
  if(NOT BUILD_TESTING)
    message(FATAL_ERROR "ILLUMO_ENABLE_COVERAGE requires BUILD_TESTING=ON")
  endif()
  get_property(coverage_runners GLOBAL PROPERTY ILLUMO_WORKSPACE_TEST_RUNNERS)
  if(NOT coverage_runners)
    message(FATAL_ERROR "Workspace coverage requires registered test runners")
  endif()
  list(REMOVE_DUPLICATES coverage_runners)
  set(coverage_dependencies IllumoPublicHeaderSmoke)
  set(coverage_manifest "")
  foreach(runner IN LISTS coverage_runners)
    list(APPEND coverage_dependencies ${runner}Discover)
    string(APPEND coverage_manifest "$<TARGET_FILE:${runner}>\n")
  endforeach()
  set(coverage_manifest_path "${CMAKE_BINARY_DIR}/coverage-binaries-$<CONFIG>.txt")
  file(GENERATE OUTPUT "${coverage_manifest_path}" CONTENT "${coverage_manifest}")
  find_program(ILLUMO_LLVM_PROFDATA_EXECUTABLE NAMES llvm-profdata REQUIRED)
  find_program(ILLUMO_LLVM_COV_EXECUTABLE NAMES llvm-cov REQUIRED)
  add_custom_target(IllumoCoverage
    COMMAND ${CMAKE_COMMAND}
      "-DTEST_BINARY_MANIFEST=${coverage_manifest_path}"
      "-DBINARY_DIR=${CMAKE_BINARY_DIR}"
      "-DCTEST_COMMAND=${CMAKE_CTEST_COMMAND}"
      "-DCONFIG=$<CONFIG>"
      "-DLLVM_PROFDATA=${ILLUMO_LLVM_PROFDATA_EXECUTABLE}"
      "-DLLVM_COV=${ILLUMO_LLVM_COV_EXECUTABLE}"
      "-DMINIMUM_LINE_COVERAGE=85"
      -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RunWorkspaceCoverage.cmake"
    DEPENDS ${coverage_dependencies}
    VERBATIM
    USES_TERMINAL
    COMMENT "Running combined Illumo workspace coverage"
  )
endfunction()
