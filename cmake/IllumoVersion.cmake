# Illumo build version: vYY.MM_B, for example v26.09_12.
#   YY.MM  the release, read from the repository's VERSION.txt. It changes
#          only when a release is cut (python build.py version --set YY.MM).
#   B      first-parent commits since VERSION.txt last changed, so every
#          release starts at _0 and a merged pull request counts once.
# (Not a bare VERSION file: the root is on include paths, and on Windows
# <version> would resolve to it.)
# The full form adds the commit and whether tracked files were modified:
# "v26.09_12 (1f709073, dirty)". Without Git history (a source copy, a
# shallow clone) B is 0 and the commit reads "unknown".
#
# Included, it defines illumo_compute_version(). Run as a script it writes
# the results on every build, touching each output only when it changes:
#   cmake -DSOURCE_ROOT=<dir> [-DGIT=<git>] [-DOUTPUT_SOURCE=<file.cpp>]
#         [-DOUTPUT_CMAKE=<file.cmake>] [-DPRINT=ON] -P IllumoVersion.cmake

# Sets <prefix>_RELEASE ("26.09"), <prefix>_BUILD (12), <prefix>_COMMIT
# ("1f709073" or empty), <prefix>_DIRTY (ON/OFF), <prefix>_SHORT
# ("v26.09_12"), <prefix>_FULL and <prefix>_PACKAGE ("26.09_12", the
# package-manifest form) in the caller's scope.
function(illumo_compute_version source_root git prefix)
  set(_file "${source_root}/VERSION.txt")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "Illumo version: ${_file} is missing")
  endif()
  file(READ "${_file}" _release)
  string(STRIP "${_release}" _release)
  if(NOT _release MATCHES "^[0-9][0-9]\\.(0[1-9]|1[0-2])$")
    message(FATAL_ERROR
      "Illumo version: ${_file} must hold YY.MM (for example 26.09), "
      "not '${_release}'")
  endif()

  set(_build 0)
  set(_commit "")
  set(_dirty OFF)
  if(git)
    execute_process(COMMAND "${git}" rev-parse --short=8 HEAD
      WORKING_DIRECTORY "${source_root}"
      RESULT_VARIABLE _result OUTPUT_VARIABLE _output
      OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_result EQUAL 0 AND _output MATCHES "^[0-9a-f]+$")
      set(_commit "${_output}")
    endif()
  endif()
  if(_commit)
    execute_process(COMMAND "${git}" rev-parse --is-shallow-repository
      WORKING_DIRECTORY "${source_root}"
      OUTPUT_VARIABLE _shallow OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    # A shallow clone cannot see where the release began, so it claims no
    # build number rather than a wrong one.
    if(NOT _shallow STREQUAL "true")
      # --first-parent: when VERSION.txt arrives through a merge, the release
      # begins at that merge, not at the side branch's older commit.
      execute_process(
        COMMAND "${git}" log -1 --first-parent --format=%H -- VERSION.txt
        WORKING_DIRECTORY "${source_root}"
        RESULT_VARIABLE _result OUTPUT_VARIABLE _base
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
      # An uncommitted VERSION.txt means the release starts with this build.
      if(_result EQUAL 0 AND _base MATCHES "^[0-9a-f]+$")
        execute_process(
          COMMAND "${git}" rev-list --count --first-parent "${_base}..HEAD"
          WORKING_DIRECTORY "${source_root}"
          RESULT_VARIABLE _result OUTPUT_VARIABLE _output
          OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(_result EQUAL 0 AND _output MATCHES "^[0-9]+$")
          set(_build "${_output}")
        endif()
      endif()
    endif()
    # --no-optional-locks keeps a build from taking index.lock while someone
    # commits; untracked files (build trees, scratch) do not count as dirty.
    execute_process(
      COMMAND "${git}" --no-optional-locks status --porcelain
        --untracked-files=no
      WORKING_DIRECTORY "${source_root}"
      RESULT_VARIABLE _result OUTPUT_VARIABLE _output
      OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_result EQUAL 0 AND NOT _output STREQUAL "")
      set(_dirty ON)
    endif()
  endif()

  set(_short "v${_release}_${_build}")
  if(_commit)
    set(_identity "${_commit}")
  else()
    set(_identity "unknown")
  endif()
  if(_dirty)
    string(APPEND _identity ", dirty")
  endif()
  set(${prefix}_RELEASE "${_release}" PARENT_SCOPE)
  set(${prefix}_BUILD "${_build}" PARENT_SCOPE)
  set(${prefix}_COMMIT "${_commit}" PARENT_SCOPE)
  set(${prefix}_DIRTY "${_dirty}" PARENT_SCOPE)
  set(${prefix}_SHORT "${_short}" PARENT_SCOPE)
  set(${prefix}_FULL "${_short} (${_identity})" PARENT_SCOPE)
  set(${prefix}_PACKAGE "${_release}_${_build}" PARENT_SCOPE)
endfunction()

# Writes content to path only when it differs, so an unchanged version does
# not recompile or relink anything.
function(illumo_write_if_different path content)
  if(EXISTS "${path}")
    file(READ "${path}" _existing)
    if(_existing STREQUAL content)
      return()
    endif()
  endif()
  file(WRITE "${path}" "${content}")
endfunction()

# Writes the BuildInfo definitions and, optionally, a CMake file other build
# scripts include (package staging reads ILLUMO_VERSION_PACKAGE from it).
function(illumo_write_version_outputs source_root git output_source output_cmake)
  illumo_compute_version("${source_root}" "${git}" _version)
  if(_version_DIRTY)
    set(_dirty true)
  else()
    set(_dirty false)
  endif()
  if(output_source)
    illumo_write_if_different("${output_source}"
"// Generated by cmake/IllumoVersion.cmake on every build; do not edit.
#include <Illumo/Foundation/BuildInfo.h>

const char* const BuildInfo::Release = \"${_version_RELEASE}\";
const unsigned BuildInfo::BuildNumber = ${_version_BUILD};
const char* const BuildInfo::Commit = \"${_version_COMMIT}\";
const bool BuildInfo::Dirty = ${_dirty};
const char* const BuildInfo::VersionNumber = \"${_version_SHORT}\";
const char* const BuildInfo::FullVersion = \"${_version_FULL}\";
")
  endif()
  if(output_cmake)
    illumo_write_if_different("${output_cmake}"
"# Generated by cmake/IllumoVersion.cmake on every build; do not edit.
set(ILLUMO_VERSION_RELEASE \"${_version_RELEASE}\")
set(ILLUMO_VERSION_BUILD \"${_version_BUILD}\")
set(ILLUMO_VERSION_SHORT \"${_version_SHORT}\")
set(ILLUMO_VERSION_FULL \"${_version_FULL}\")
set(ILLUMO_VERSION_PACKAGE \"${_version_PACKAGE}\")
")
  endif()
  set(ILLUMO_VERSION_FULL "${_version_FULL}" PARENT_SCOPE)
endfunction()

if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
  if(NOT SOURCE_ROOT)
    message(FATAL_ERROR "IllumoVersion.cmake needs -DSOURCE_ROOT=<dir>")
  endif()
  if(NOT DEFINED GIT)
    find_program(GIT NAMES git)
  endif()
  if(PRINT)
    illumo_compute_version("${SOURCE_ROOT}" "${GIT}" _version)
    # stdout, one field per line, so build.py can parse it.
    execute_process(COMMAND "${CMAKE_COMMAND}" -E echo
      "release=${_version_RELEASE}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E echo
      "build=${_version_BUILD}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E echo
      "commit=${_version_COMMIT}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E echo
      "dirty=${_version_DIRTY}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E echo
      "short=${_version_SHORT}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E echo
      "full=${_version_FULL}")
  else()
    illumo_write_version_outputs("${SOURCE_ROOT}" "${GIT}"
      "${OUTPUT_SOURCE}" "${OUTPUT_CMAKE}")
  endif()
endif()
