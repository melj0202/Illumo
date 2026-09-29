# @PROJECT_NAME@'s WASM module, included by IllumoGuest/CMakeLists.txt in the
# WASI guest build (the root CMakeLists.txt lists this directory in
# ILLUMO_PROGRAMS). Paths are absolute: this file runs in IllumoGuest's
# directory scope.
set(_app "${CMAKE_CURRENT_LIST_DIR}")
illumo_add_guest(@PROJECT_NAME@
  "${_app}/Source/Wasm/SpinningCubeProgram.cpp"
  "${_app}/Source/SpinningCubeConfig.cpp"
  "${_app}/Source/SpinningCubeScene.cpp")
target_include_directories(@PROJECT_NAME@ PRIVATE "${_app}/Source")
target_include_directories(@PROJECT_NAME@ SYSTEM PRIVATE
  "${ILLUMO_ROOT}/Illumo/thirdparty/tracy-0.14.1/public")
target_link_libraries(@PROJECT_NAME@ PRIVATE IllumoGuestContent IllumoGuestEngine)
unset(_app)
