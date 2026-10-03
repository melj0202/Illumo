# Assembles a distribution of a built runtime (D-E38): the runtime and its
# helpers, settings and notices loose, the engine's files as engine.ilpk and
# the chosen applications as apps/<name>.ilpk (all staged ones by default).
# Nothing else is loose, bar each one's shortcut and icon (Windows).
#   cmake -DRUNTIME_DIR=<build>/<config> -DOUT_DIR=<build>/dist/<config>
#         -DPACK=<IllumoPack> -DENGINE_ASSETS=<Illumo/Assets>
#         -DENGINE_SHADERS=<Illumo/Shader> -DENGINE_MANIFEST=<illumo.json>
#         -DVERSION_CMAKE=<Generated/IllumoVersion.cmake> -DSCRATCH=<dir>
#         [-DAPPS=<name;name>] -P IllumoDistribution.cmake
foreach(_required RUNTIME_DIR OUT_DIR PACK ENGINE_ASSETS ENGINE_SHADERS
    ENGINE_MANIFEST VERSION_CMAKE SCRATCH)
  if(NOT DEFINED ${_required})
    message(FATAL_ERROR "IllumoDistribution.cmake needs -D${_required}")
  endif()
endforeach()

function(_illumo_pack directory archive)
  execute_process(COMMAND "${PACK}" "${directory}" "${archive}"
    RESULT_VARIABLE _packed)
  if(NOT _packed EQUAL 0)
    message(FATAL_ERROR "IllumoPack could not pack ${directory}")
  endif()
  execute_process(COMMAND "${PACK}" --verify "${archive}"
    RESULT_VARIABLE _verified)
  if(NOT _verified EQUAL 0)
    message(FATAL_ERROR "IllumoPack could not verify ${archive}")
  endif()
endfunction()

file(REMOVE_RECURSE "${OUT_DIR}" "${SCRATCH}")
file(MAKE_DIRECTORY "${OUT_DIR}/apps")

# The runtime, the module compiler it launches, their DLLs, the default
# settings and the third-party notices.
foreach(_file IllumoRuntime.exe IllumoWasmCompiler.exe envvars.json
    THIRD_PARTY_NOTICES.md)
  if(NOT EXISTS "${RUNTIME_DIR}/${_file}")
    message(FATAL_ERROR "${RUNTIME_DIR}/${_file} is missing; build IllumoRuntime first")
  endif()
  file(COPY "${RUNTIME_DIR}/${_file}" DESTINATION "${OUT_DIR}")
endforeach()
file(GLOB _libraries "${RUNTIME_DIR}/*.dll")
file(COPY ${_libraries} DESTINATION "${OUT_DIR}")
file(COPY "${RUNTIME_DIR}/licenses" DESTINATION "${OUT_DIR}")

# engine.ilpk: what development builds stage as Assets/ at its root, plus
# Shader/, under the stamped engine manifest.
set(_engine "${SCRATCH}/engine")
file(COPY "${ENGINE_ASSETS}/" DESTINATION "${_engine}")
file(COPY "${ENGINE_SHADERS}/" DESTINATION "${_engine}/Shader")
execute_process(COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE=${ENGINE_MANIFEST}"
    "-DDESTINATION=${_engine}/illumo.json"
    "-DVERSION_CMAKE=${VERSION_CMAKE}"
    -P "${CMAKE_CURRENT_LIST_DIR}/IllumoStageManifest.cmake"
  RESULT_VARIABLE _stamped)
if(NOT _stamped EQUAL 0)
  message(FATAL_ERROR "Could not stamp the engine manifest")
endif()
_illumo_pack("${_engine}" "${OUT_DIR}/engine.ilpk")

# The applications to ship: -DAPPS=<a;b> or ILLUMO_DIST_APPS=<a,b> (what
# `python build.py dist --apps` sets) names the ones to pack; none means every
# staged application. A name that is not staged fails, so a typo cannot ship
# a distribution without the application it was meant to carry.
set(_staged)
file(GLOB _apps LIST_DIRECTORIES true "${RUNTIME_DIR}/apps/*")
foreach(_app IN LISTS _apps)
  if(IS_DIRECTORY "${_app}" AND EXISTS "${_app}/illumo.json")
    get_filename_component(_name "${_app}" NAME)
    list(APPEND _staged "${_name}")
  endif()
endforeach()
if(NOT _staged)
  message(FATAL_ERROR "No staged applications in ${RUNTIME_DIR}/apps")
endif()
if(NOT DEFINED APPS)
  string(REPLACE "," ";" APPS "$ENV{ILLUMO_DIST_APPS}")
endif()
set(_chosen ${_staged})
if(APPS)
  list(REMOVE_DUPLICATES APPS)
  foreach(_name IN LISTS APPS)
    if(NOT _name IN_LIST _staged)
      list(JOIN _staged ", " _available)
      message(FATAL_ERROR "'${_name}' is not a staged application (staged: ${_available})")
    endif()
  endforeach()
  set(_chosen ${APPS})
endif()

# Each chosen application, as apps/<name>.ilpk.
set(_packed_apps)
foreach(_name IN LISTS _chosen)
  _illumo_pack("${RUNTIME_DIR}/apps/${_name}" "${OUT_DIR}/apps/${_name}.ilpk")
  list(APPEND _packed_apps "${_name}")
endforeach()

# A shortcut beside the runtime for each packed application, named after its
# title and wearing its app.ico when it has one (copied beside it, as a
# shortcut's icon must be a file). A .lnk, because a link cannot carry --app;
# it keeps a relative target, so the folder can move. Best effort: a failure
# warns.
set(_shortcuts)
if(CMAKE_HOST_WIN32)
  find_program(_powershell NAMES powershell pwsh)
  if(NOT _powershell)
    message(WARNING "PowerShell not found; no application shortcuts")
  endif()
endif()
if(CMAKE_HOST_WIN32 AND _powershell)
  foreach(_name IN LISTS _packed_apps)
    set(_title "${_name}")
    file(READ "${RUNTIME_DIR}/apps/${_name}/illumo.json" _manifest)
    string(JSON _manifest_title ERROR_VARIABLE _no_title GET "${_manifest}" title)
    if(_no_title STREQUAL "NOTFOUND" AND NOT _manifest_title STREQUAL "")
      string(REGEX REPLACE "[<>:\"/\\\\|?*]" "" _title "${_manifest_title}")
    endif()
    set(_icon)
    if(EXISTS "${RUNTIME_DIR}/apps/${_name}/app.ico")
      file(COPY_FILE "${RUNTIME_DIR}/apps/${_name}/app.ico" "${OUT_DIR}/${_title}.ico")
      set(_icon -Icon "${OUT_DIR}/${_title}.ico")
    endif()
    execute_process(COMMAND "${_powershell}" -NoProfile -NonInteractive
        -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_LIST_DIR}/IllumoShortcut.ps1"
        -Path "${OUT_DIR}/${_title}.lnk" -Target "${OUT_DIR}/IllumoRuntime.exe"
        -Arguments "--app ${_name}" -Description "${_title}" ${_icon}
      RESULT_VARIABLE _linked ERROR_VARIABLE _link_error)
    if(_linked EQUAL 0)
      list(APPEND _shortcuts "${_title}.lnk")
    else()
      message(WARNING "Could not create the ${_title} shortcut: ${_link_error}")
    endif()
  endforeach()
endif()

file(REMOVE_RECURSE "${SCRATCH}")
list(JOIN _packed_apps ", " _app_list)
list(JOIN _shortcuts ", " _shortcut_list)
set(_shortcut_note)
if(_shortcuts)
  set(_shortcut_note "; shortcuts ${_shortcut_list}")
endif()
message(STATUS "Distribution in ${OUT_DIR}: engine.ilpk; apps ${_app_list}${_shortcut_note}")