# Assembles a distribution of a built runtime (D-E38): the runtime and its
# helpers, settings and notices loose, the engine's files as engine.ilpk and
# every staged application as apps/<name>.ilpk. Nothing else is loose.
#   cmake -DRUNTIME_DIR=<build>/<config> -DOUT_DIR=<build>/dist/<config>
#         -DPACK=<IllumoPack> -DENGINE_ASSETS=<Illumo/Assets>
#         -DENGINE_SHADERS=<Illumo/Shader> -DENGINE_MANIFEST=<illumo.json>
#         -DVERSION_CMAKE=<Generated/IllumoVersion.cmake> -DSCRATCH=<dir>
#         -P IllumoDistribution.cmake
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

# Every staged application, as apps/<name>.ilpk.
file(GLOB _apps LIST_DIRECTORIES true "${RUNTIME_DIR}/apps/*")
set(_packed_apps)
foreach(_app IN LISTS _apps)
  if(IS_DIRECTORY "${_app}" AND EXISTS "${_app}/illumo.json")
    get_filename_component(_name "${_app}" NAME)
    _illumo_pack("${_app}" "${OUT_DIR}/apps/${_name}.ilpk")
    list(APPEND _packed_apps "${_name}")
  endif()
endforeach()
if(NOT _packed_apps)
  message(FATAL_ERROR "No staged applications in ${RUNTIME_DIR}/apps")
endif()

file(REMOVE_RECURSE "${SCRATCH}")
list(JOIN _packed_apps ", " _app_list)
message(STATUS "Distribution in ${OUT_DIR}: engine.ilpk; apps ${_app_list}")
