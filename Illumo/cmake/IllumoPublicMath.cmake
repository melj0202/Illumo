# Preserve <glm/...> public includes without exporting the entire vendor tree.
function(illumo_prepare_public_math output_variable)
  set(source_root "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty")
  set(include_root "${CMAKE_CURRENT_BINARY_DIR}/public-thirdparty")
  file(GLOB_RECURSE math_headers CONFIGURE_DEPENDS
    RELATIVE "${source_root}"
    "${source_root}/glm/*.h"
    "${source_root}/glm/*.hpp"
    "${source_root}/glm/*.inl")
  if(NOT "glm/glm.hpp" IN_LIST math_headers)
    message(FATAL_ERROR "Public math headers require the vendored GLM source")
  endif()
  foreach(header IN LISTS math_headers)
    # configure_file tracks content changes and preserves unchanged timestamps.
    configure_file("${source_root}/${header}" "${include_root}/${header}" COPYONLY)
  endforeach()
  file(GLOB_RECURSE previous_headers RELATIVE "${include_root}"
    "${include_root}/glm/*.h"
    "${include_root}/glm/*.hpp"
    "${include_root}/glm/*.inl")
  foreach(header IN LISTS previous_headers)
    if(NOT header IN_LIST math_headers)
      file(REMOVE "${include_root}/${header}")
    endif()
  endforeach()
  set(${output_variable} "${include_root}" PARENT_SCOPE)
endfunction()
