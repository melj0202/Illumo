# Separate configure tree: never mix native objects and wasm32 objects.
get_filename_component(_illumo_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT ILLUMO_WASM_TOOLS)
  set(ILLUMO_WASM_TOOLS "${_illumo_root}/build-wasm-tools")
endif()
set(WASI_SDK_PREFIX "${ILLUMO_WASM_TOOLS}/wasi-sdk-34.0-x86_64-windows")
include("${WASI_SDK_PREFIX}/share/cmake/wasi-sdk-p1.cmake")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
