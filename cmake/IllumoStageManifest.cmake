# Stages an application's illumo.json with the build's version, so source
# manifests carry no version of their own (cmake/IllumoVersion.cmake).
#   cmake -DSOURCE=<illumo.json> -DDESTINATION=<apps/name/illumo.json>
#         -DVERSION_CMAKE=<Generated/IllumoVersion.cmake> -P IllumoStageManifest.cmake
include("${VERSION_CMAKE}")
file(READ "${SOURCE}" _manifest)
# ERROR_VARIABLE reads NOTFOUND when the lookup succeeded.
string(JSON _existing ERROR_VARIABLE _lookup GET "${_manifest}" version)
if(_lookup STREQUAL "NOTFOUND")
  message(FATAL_ERROR
    "${SOURCE} sets \"version\"; the build supplies it from VERSION.txt")
endif()
string(JSON _manifest SET "${_manifest}" version "\"${ILLUMO_VERSION_PACKAGE}\"")
include("${CMAKE_CURRENT_LIST_DIR}/IllumoVersion.cmake")
illumo_write_if_different("${DESTINATION}" "${_manifest}\n")
