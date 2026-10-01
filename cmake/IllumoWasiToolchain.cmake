# Separate configure tree: never mix native objects and wasm32 objects.
get_filename_component(_illumo_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT ILLUMO_WASM_TOOLS)
  set(ILLUMO_WASM_TOOLS "${_illumo_root}/build-wasm-tools")
endif()
# Compiler checks configure their own projects with this file; pass them the
# tool directory too, or a toolchain outside the source tree is not found.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES ILLUMO_WASM_TOOLS)
set(WASI_SDK_PREFIX "${ILLUMO_WASM_TOOLS}/wasi-sdk-34.0-x86_64-windows")
include("${WASI_SDK_PREFIX}/share/cmake/wasi-sdk-p1.cmake")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
