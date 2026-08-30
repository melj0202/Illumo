include_guard(GLOBAL)

function(_illumo_runtime_output_directory output_variable)
  if(DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY AND NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY STREQUAL "")
    set(runtime_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
  elseif(CMAKE_CONFIGURATION_TYPES)
    set(runtime_directory "${CMAKE_BINARY_DIR}/$<CONFIG>")
  else()
    set(runtime_directory "${CMAKE_BINARY_DIR}")
  endif()
  set(${output_variable} "${runtime_directory}" PARENT_SCOPE)
endfunction()

function(illumo_stage_runtime target_name)
  if(NOT DEFINED ILLUMO_LIBRARY_SOURCE_DIR)
    message(FATAL_ERROR "ILLUMO_LIBRARY_SOURCE_DIR is required for staging")
  endif()

  file(GLOB_RECURSE illumo_runtime_inputs CONFIGURE_DEPENDS
    "${ILLUMO_LIBRARY_SOURCE_DIR}/Shader/*"
    "${ILLUMO_LIBRARY_SOURCE_DIR}/Assets/*")
  list(APPEND illumo_runtime_inputs
    "${ILLUMO_LIBRARY_SOURCE_DIR}/../THIRD_PARTY_NOTICES.md"
    "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/freetype-2.13.3/LICENSE.TXT"
    "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/freetype-2.13.3/docs/FTL.TXT"
    "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/glew-2.1.0/LICENSE.txt"
    "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/glfw-3.4/LICENSE.md"
    "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/glm/copying.txt"
    "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/json/LICENSE.MIT"
    "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/stb/LICENSE"
    "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/tracy-0.13.1/LICENSE")
  target_sources(${target_name} PRIVATE ${illumo_runtime_inputs})
  set_source_files_properties(${illumo_runtime_inputs}
    PROPERTIES HEADER_FILE_ONLY TRUE)

  _illumo_runtime_output_directory(runtime_directory)
  set(stage_target "${target_name}RuntimeStage")
  add_custom_target(${stage_target}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${ILLUMO_LIBRARY_SOURCE_DIR}/Shader"
      "${runtime_directory}/Shader"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${ILLUMO_LIBRARY_SOURCE_DIR}/Assets"
      "${runtime_directory}/Assets"
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "${runtime_directory}/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ILLUMO_LIBRARY_SOURCE_DIR}/../THIRD_PARTY_NOTICES.md"
      "${runtime_directory}/THIRD_PARTY_NOTICES.md"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/freetype-2.13.3/LICENSE.TXT"
      "${runtime_directory}/licenses/FreeType-LICENSE.TXT"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/freetype-2.13.3/docs/FTL.TXT"
      "${runtime_directory}/licenses/FreeType-FTL.TXT"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/glew-2.1.0/LICENSE.txt"
      "${runtime_directory}/licenses/GLEW-LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/glfw-3.4/LICENSE.md"
      "${runtime_directory}/licenses/GLFW-LICENSE.md"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/glm/copying.txt"
      "${runtime_directory}/licenses/GLM-LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/json/LICENSE.MIT"
      "${runtime_directory}/licenses/nlohmann-json-LICENSE.MIT"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/stb/LICENSE"
      "${runtime_directory}/licenses/stb-LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/tracy-0.13.1/LICENSE"
      "${runtime_directory}/licenses/Tracy-LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ILLUMO_LIBRARY_SOURCE_DIR}/Assets/Fonts/Handjet/OFL.txt"
      "${runtime_directory}/licenses/Handjet-OFL.txt"
    VERBATIM
    COMMENT "Staging Illumo runtime assets and licenses for ${target_name}"
  )
  set_target_properties(${stage_target} PROPERTIES FOLDER "staging")
  add_dependencies(${target_name} ${stage_target})
endfunction()

function(illumo_stage_shaders target_name source_directory)
  file(GLOB_RECURSE shader_inputs CONFIGURE_DEPENDS
    "${source_directory}/*")
  target_sources(${target_name} PRIVATE ${shader_inputs})
  set_source_files_properties(${shader_inputs}
    PROPERTIES HEADER_FILE_ONLY TRUE)

  _illumo_runtime_output_directory(runtime_directory)
  set(stage_target "${target_name}ShaderStage")
  add_custom_target(${stage_target}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${source_directory}"
      "${runtime_directory}/Shader"
    VERBATIM
    COMMENT "Staging Illumo shaders for ${target_name}")
  set_target_properties(${stage_target} PROPERTIES FOLDER "staging")
  add_dependencies(${target_name} ${stage_target})
endfunction()

function(illumo_stage_default_file target_name source_file destination_name)
  target_sources(${target_name} PRIVATE "${source_file}")
  set_source_files_properties("${source_file}"
    PROPERTIES HEADER_FILE_ONLY TRUE)

  _illumo_runtime_output_directory(runtime_directory)
  set(stage_target "${target_name}DefaultFileStage")
  add_custom_target(${stage_target}
    COMMAND ${CMAKE_COMMAND}
      "-DSOURCE=${source_file}"
      "-DDESTINATION=${runtime_directory}/${destination_name}"
      -P "${ILLUMO_LIBRARY_SOURCE_DIR}/cmake/CopyIfMissing.cmake"
    VERBATIM
    COMMENT "Seeding ${destination_name} for ${target_name}")
  set_target_properties(${stage_target} PROPERTIES FOLDER "staging")
  add_dependencies(${target_name} ${stage_target})
endfunction()

function(illumo_stage_msvc_asan target_name)
  if(NOT MSVC)
    return()
  endif()
  get_filename_component(msvc_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
  find_file(illumo_asan_dll "clang_rt.asan_dynamic-x86_64.dll"
    PATHS "${msvc_bin_dir}")
  if(illumo_asan_dll)
    add_custom_command(TARGET ${target_name} POST_BUILD
      COMMAND $<$<CONFIG:Debug>:${CMAKE_COMMAND}>
        $<$<CONFIG:Debug>:-E>
        $<$<CONFIG:Debug>:copy_if_different>
        "$<$<CONFIG:Debug>:${illumo_asan_dll}>"
        "$<$<CONFIG:Debug>:$<TARGET_FILE_DIR:${target_name}>>"
      COMMENT "Staging the Debug ASan runtime for ${target_name}"
    )
  endif()
endfunction()
