# Fails when first-party C++ uses try, catch or throw. The workspace builds
# without exceptions (docs/contributing.md); Clang refuses them outright, but
# MSVC only warns (C4530, an error for first-party targets), so this check
# keeps both toolchains honest, including code no configuration compiles.
#
# cmake -DSOURCE_DIR=<workspace> -P CheckNoExceptions.cmake
if(NOT SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(_roots Illumo IllumoGuest IllumoGame IllEd IllMeshViewer Templates)
set(_files)
foreach(_root IN LISTS _roots)
  file(GLOB_RECURSE _found
    "${SOURCE_DIR}/${_root}/*.cpp" "${SOURCE_DIR}/${_root}/*.h"
    "${SOURCE_DIR}/${_root}/*.hpp" "${SOURCE_DIR}/${_root}/*.inc")
  list(APPEND _files ${_found})
endforeach()
list(FILTER _files EXCLUDE REGEX "/thirdparty/")

set(_violations 0)
foreach(_file IN LISTS _files)
  file(STRINGS "${_file}" _lines)
  set(_number 0)
  set(_inBlockComment FALSE)
  foreach(_line IN LISTS _lines)
    math(EXPR _number "${_number} + 1")
    set(_code "${_line}")
    # Block comments: the rest of a line inside one is ignored.
    if(_inBlockComment)
      string(FIND "${_code}" "*/" _end)
      if(_end EQUAL -1)
        continue()
      endif()
      math(EXPR _end "${_end} + 2")
      string(SUBSTRING "${_code}" ${_end} -1 _code)
      set(_inBlockComment FALSE)
    endif()
    # Strings and character literals, then line and block comments.
    string(REGEX REPLACE "\"([^\"\\\\]|\\\\.)*\"" "\"\"" _code "${_code}")
    string(REGEX REPLACE "'([^'\\\\]|\\\\.)*'" "''" _code "${_code}")
    string(REGEX REPLACE "/\\*.*\\*/" "" _code "${_code}")
    string(REGEX REPLACE "//.*$" "" _code "${_code}")
    string(FIND "${_code}" "/*" _start)
    if(NOT _start EQUAL -1)
      string(SUBSTRING "${_code}" 0 ${_start} _code)
      set(_inBlockComment TRUE)
    endif()
    if(_code MATCHES "(^|[^A-Za-z0-9_])(try|catch|throw)([^A-Za-z0-9_]|$)")
      file(RELATIVE_PATH _relative "${SOURCE_DIR}" "${_file}")
      message(STATUS "${_relative}:${_number}: ${_line}")
      math(EXPR _violations "${_violations} + 1")
    endif()
  endforeach()
endforeach()

list(LENGTH _files _count)
if(_violations GREATER 0)
  message(FATAL_ERROR
    "${_violations} use(s) of try/catch/throw in first-party C++; the "
    "workspace builds without exceptions")
endif()
message(STATUS "No exception handling in ${_count} first-party C++ files")
