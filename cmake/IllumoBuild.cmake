include_guard(GLOBAL)

option(ILLUMO_ENABLE_TRACY
  "Enable Tracy instrumentation in optimized builds" OFF)
option(ILLUMO_ENABLE_COVERAGE
  "Instrument both Illumo test runners for LLVM coverage" OFF)

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
  )
  if(ILLUMO_ENABLE_TRACY)
    target_compile_definitions(${target_name} PRIVATE TRACY_ENABLE)
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
