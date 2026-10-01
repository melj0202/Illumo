include_guard(GLOBAL)

# Third-party piece of the Direct3D 12 backend (docs/d3d12-backend-plan.md):
# SPIRV-Cross from the Vulkan SDK 1.4.363.0 tag, translating the SPIR-V the
# shared GLSL front end produces into HLSL. Only the HLSL path is built;
# upstream build files are not vendored.
#   illumo_spirv_cross  SPIR-V to HLSL, errors routed to the embedding program

set(_illumo_spirv_cross_dir
  "${CMAKE_CURRENT_LIST_DIR}/../thirdparty/spirv-cross-1.4.363")

add_library(illumo_spirv_cross STATIC
  "${_illumo_spirv_cross_dir}/spirv_cfg.cpp"
  "${_illumo_spirv_cross_dir}/spirv_cross.cpp"
  "${_illumo_spirv_cross_dir}/spirv_cross_parsed_ir.cpp"
  "${_illumo_spirv_cross_dir}/spirv_glsl.cpp"
  "${_illumo_spirv_cross_dir}/spirv_hlsl.cpp"
  "${_illumo_spirv_cross_dir}/spirv_parser.cpp")
target_include_directories(illumo_spirv_cross SYSTEM PUBLIC
  "${_illumo_spirv_cross_dir}")
# No exceptions anywhere (D-F3): an error calls IllumoSpirvCrossFatal, which
# the backend defines to report it and end the process.
target_compile_definitions(illumo_spirv_cross PUBLIC
  SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS
  ILLUMO_SPIRV_CROSS_FATAL=IllumoSpirvCrossFatal)
target_compile_features(illumo_spirv_cross PRIVATE cxx_std_17)
if(MSVC)
  target_compile_options(illumo_spirv_cross PRIVATE /W0 /bigobj)
  if(ILLUMO_ENABLE_ASAN AND NOT ILLUMO_ENABLE_COVERAGE)
    target_compile_options(illumo_spirv_cross PRIVATE
      $<$<CONFIG:Debug>:/fsanitize=address>)
  endif()
else()
  target_compile_options(illumo_spirv_cross PRIVATE -w)
endif()
set_target_properties(illumo_spirv_cross PROPERTIES FOLDER "thirdparty")

unset(_illumo_spirv_cross_dir)
