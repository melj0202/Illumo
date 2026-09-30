include_guard(GLOBAL)

# Third-party pieces of the Vulkan backend (docs/vulkan-backend-plan.md),
# all pinned to Vulkan SDK 1.4.363.0. Upstream build files are not vendored:
# these targets compile exactly the sources the backend needs.
#   illumo_vulkan_headers  Vulkan C headers, prototypes off (volk loads them)
#   illumo_volk            loads vulkan-1.dll / libvulkan at run time
#   illumo_vma             Vulkan Memory Allocator implementation
#   illumo_glslang         GLSL to SPIR-V, without HLSL or the optimizer

set(_illumo_tp "${CMAKE_CURRENT_LIST_DIR}/../thirdparty")
set(_illumo_vk_headers "${_illumo_tp}/vulkan-headers-1.4.363/include")
set(_illumo_volk_dir "${_illumo_tp}/volk-1.4.363")
set(_illumo_vma_dir "${_illumo_tp}/vma-3.4.0/include")
set(_illumo_glslang_dir "${_illumo_tp}/glslang-16.6.0")

add_library(illumo_vulkan_headers INTERFACE)
target_include_directories(illumo_vulkan_headers SYSTEM INTERFACE
  "${_illumo_vk_headers}"
  "${_illumo_volk_dir}"
  "${_illumo_vma_dir}")
# No VK_USE_PLATFORM_*: GLFW creates the window surface, and the Win32
# platform header would pull <windows.h> into every Vulkan source.
target_compile_definitions(illumo_vulkan_headers INTERFACE VK_NO_PROTOTYPES)

add_library(illumo_volk STATIC "${_illumo_volk_dir}/volk.c")
target_link_libraries(illumo_volk PUBLIC illumo_vulkan_headers)
if(UNIX)
  target_link_libraries(illumo_volk PRIVATE ${CMAKE_DL_LIBS})
endif()
set_target_properties(illumo_volk PROPERTIES FOLDER "thirdparty")

set(_illumo_vma_source "${CMAKE_CURRENT_BINARY_DIR}/Generated/IllumoVma.cpp")
file(WRITE "${_illumo_vma_source}.in"
  "// Generated: the one translation unit holding the VMA implementation.\n"
  "#include <volk.h>\n"
  "#define VMA_IMPLEMENTATION\n"
  "#define VMA_STATIC_VULKAN_FUNCTIONS 0\n"
  "#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1\n"
  "#include <vk_mem_alloc.h>\n")
configure_file("${_illumo_vma_source}.in" "${_illumo_vma_source}" COPYONLY)
add_library(illumo_vma STATIC "${_illumo_vma_source}")
target_link_libraries(illumo_vma PUBLIC illumo_vulkan_headers)
target_compile_features(illumo_vma PRIVATE cxx_std_17)
if(MSVC)
  target_compile_options(illumo_vma PRIVATE /W0)
else()
  target_compile_options(illumo_vma PRIVATE -w)
endif()
set_target_properties(illumo_vma PROPERTIES FOLDER "thirdparty")

# glslang: the version header its build normally generates from CHANGES.md.
set(_illumo_glslang_generated "${CMAKE_CURRENT_BINARY_DIR}/Generated/glslang")
file(MAKE_DIRECTORY "${_illumo_glslang_generated}/glslang")
file(WRITE "${_illumo_glslang_generated}/glslang/build_info.h.in"
  "#ifndef GLSLANG_BUILD_INFO\n"
  "#define GLSLANG_BUILD_INFO\n"
  "#define GLSLANG_VERSION_MAJOR 16\n"
  "#define GLSLANG_VERSION_MINOR 6\n"
  "#define GLSLANG_VERSION_PATCH 0\n"
  "#define GLSLANG_VERSION_FLAVOR \"\"\n"
  "#define GLSLANG_VERSION_GREATER_THAN(major, minor, patch) \\\n"
  "  ((GLSLANG_VERSION_MAJOR) > (major) || ((major) == GLSLANG_VERSION_MAJOR && \\\n"
  "  ((GLSLANG_VERSION_MINOR) > (minor) || ((minor) == GLSLANG_VERSION_MINOR && \\\n"
  "   (GLSLANG_VERSION_PATCH) > (patch)))))\n"
  "#define GLSLANG_VERSION_GREATER_OR_EQUAL_TO(major, minor, patch) \\\n"
  "  ((GLSLANG_VERSION_MAJOR) > (major) || ((major) == GLSLANG_VERSION_MAJOR && \\\n"
  "  ((GLSLANG_VERSION_MINOR) > (minor) || ((minor) == GLSLANG_VERSION_MINOR && \\\n"
  "   (GLSLANG_VERSION_PATCH >= (patch))))))\n"
  "#define GLSLANG_VERSION_LESS_THAN(major, minor, patch) \\\n"
  "  ((GLSLANG_VERSION_MAJOR) < (major) || ((major) == GLSLANG_VERSION_MAJOR && \\\n"
  "  ((GLSLANG_VERSION_MINOR) < (minor) || ((minor) == GLSLANG_VERSION_MINOR && \\\n"
  "   (GLSLANG_VERSION_PATCH) < (patch)))))\n"
  "#define GLSLANG_VERSION_LESS_OR_EQUAL_TO(major, minor, patch) \\\n"
  "  ((GLSLANG_VERSION_MAJOR) < (major) || ((major) == GLSLANG_VERSION_MAJOR && \\\n"
  "  ((GLSLANG_VERSION_MINOR) < (minor) || ((minor) == GLSLANG_VERSION_MINOR && \\\n"
  "   (GLSLANG_VERSION_PATCH <= (patch))))))\n"
  "#endif\n")
configure_file("${_illumo_glslang_generated}/glslang/build_info.h.in"
  "${_illumo_glslang_generated}/glslang/build_info.h" COPYONLY)

set(_illumo_gl "${_illumo_glslang_dir}/glslang")
set(_illumo_glslang_sources
  "${_illumo_gl}/GenericCodeGen/CodeGen.cpp"
  "${_illumo_gl}/GenericCodeGen/Link.cpp"
  "${_illumo_gl}/MachineIndependent/glslang_tab.cpp"
  "${_illumo_gl}/MachineIndependent/attribute.cpp"
  "${_illumo_gl}/MachineIndependent/Constant.cpp"
  "${_illumo_gl}/MachineIndependent/iomapper.cpp"
  "${_illumo_gl}/MachineIndependent/InfoSink.cpp"
  "${_illumo_gl}/MachineIndependent/Initialize.cpp"
  "${_illumo_gl}/MachineIndependent/IntermTraverse.cpp"
  "${_illumo_gl}/MachineIndependent/Intermediate.cpp"
  "${_illumo_gl}/MachineIndependent/ParseContextBase.cpp"
  "${_illumo_gl}/MachineIndependent/ParseHelper.cpp"
  "${_illumo_gl}/MachineIndependent/PoolAlloc.cpp"
  "${_illumo_gl}/MachineIndependent/RemoveTree.cpp"
  "${_illumo_gl}/MachineIndependent/Scan.cpp"
  "${_illumo_gl}/MachineIndependent/ShaderLang.cpp"
  "${_illumo_gl}/MachineIndependent/SpirvIntrinsics.cpp"
  "${_illumo_gl}/MachineIndependent/SymbolTable.cpp"
  "${_illumo_gl}/MachineIndependent/Versions.cpp"
  "${_illumo_gl}/MachineIndependent/intermOut.cpp"
  "${_illumo_gl}/MachineIndependent/limits.cpp"
  "${_illumo_gl}/MachineIndependent/linkValidate.cpp"
  "${_illumo_gl}/MachineIndependent/parseConst.cpp"
  "${_illumo_gl}/MachineIndependent/reflection.cpp"
  "${_illumo_gl}/MachineIndependent/preprocessor/Pp.cpp"
  "${_illumo_gl}/MachineIndependent/preprocessor/PpAtom.cpp"
  "${_illumo_gl}/MachineIndependent/preprocessor/PpContext.cpp"
  "${_illumo_gl}/MachineIndependent/preprocessor/PpScanner.cpp"
  "${_illumo_gl}/MachineIndependent/preprocessor/PpTokens.cpp"
  "${_illumo_gl}/MachineIndependent/propagateNoContraction.cpp"
  "${_illumo_gl}/ResourceLimits/ResourceLimits.cpp"
  "${_illumo_glslang_dir}/SPIRV/GlslangToSpv.cpp"
  "${_illumo_glslang_dir}/SPIRV/InReadableOrder.cpp"
  "${_illumo_glslang_dir}/SPIRV/Logger.cpp"
  "${_illumo_glslang_dir}/SPIRV/SpvBuilder.cpp"
  "${_illumo_glslang_dir}/SPIRV/SpvPostProcess.cpp"
  "${_illumo_glslang_dir}/SPIRV/SpvTools.cpp"
  "${_illumo_glslang_dir}/SPIRV/doc.cpp"
  "${_illumo_glslang_dir}/SPIRV/disassemble.cpp")
if(WIN32)
  list(APPEND _illumo_glslang_sources
    "${_illumo_gl}/OSDependent/Windows/ossource.cpp")
else()
  list(APPEND _illumo_glslang_sources
    "${_illumo_gl}/OSDependent/Unix/ossource.cpp")
endif()
add_library(illumo_glslang STATIC ${_illumo_glslang_sources})
target_include_directories(illumo_glslang SYSTEM PUBLIC
  "${_illumo_glslang_dir}"
  "${_illumo_glslang_generated}")
target_compile_definitions(illumo_glslang PUBLIC ENABLE_SPIRV ENABLE_OPT=0)
if(WIN32)
  target_compile_definitions(illumo_glslang PRIVATE GLSLANG_OSINCLUDE_WIN32)
else()
  target_compile_definitions(illumo_glslang PRIVATE GLSLANG_OSINCLUDE_UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(illumo_glslang PUBLIC Threads::Threads)
endif()
target_compile_features(illumo_glslang PRIVATE cxx_std_17)
if(MSVC)
  target_compile_options(illumo_glslang PRIVATE /W0 /bigobj)
  # The MSVC STL annotates containers under ASan; a static C++ library must
  # match the Debug executables it links into.
  if(ILLUMO_ENABLE_ASAN AND NOT ILLUMO_ENABLE_COVERAGE)
    target_compile_options(illumo_glslang PRIVATE
      $<$<CONFIG:Debug>:/fsanitize=address>)
    target_compile_options(illumo_vma PRIVATE
      $<$<CONFIG:Debug>:/fsanitize=address>)
  endif()
else()
  target_compile_options(illumo_glslang PRIVATE -w)
endif()
set_target_properties(illumo_glslang PROPERTIES FOLDER "thirdparty")

unset(_illumo_tp)
unset(_illumo_vk_headers)
unset(_illumo_volk_dir)
unset(_illumo_vma_dir)
unset(_illumo_glslang_dir)
unset(_illumo_gl)
unset(_illumo_glslang_sources)
unset(_illumo_glslang_generated)
unset(_illumo_vma_source)
