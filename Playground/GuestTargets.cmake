# The Illumo Playground, the sample game for scene behaviours, as a WASM
# package module; included by IllumoGuest/CMakeLists.txt in the WASI guest
# build (one entry of ILLUMO_PROGRAMS).
set(_playground "${CMAKE_CURRENT_LIST_DIR}/Source")
illumo_add_guest(Playground
  "${_playground}/PlaygroundApplication.cpp"
  "${_playground}/PlaygroundBehaviours.cpp")
target_include_directories(Playground PRIVATE "${_playground}")
target_include_directories(Playground SYSTEM PRIVATE
  "${ILLUMO_ROOT}/Illumo/thirdparty/tracy-0.14.1/public")
target_link_libraries(Playground PRIVATE IllumoGuestContent IllumoGuestEngine)
unset(_playground)
