# @PROJECT_NAME@'s package, included by cmake/IllumoWasm.cmake once
# illumo_stage_app and IllumoRuntime exist. It stages apps/@PROJECT_ID@ beside
# IllumoRuntime, which runs it with --app @PROJECT_ID@.
set(_app "${CMAKE_CURRENT_LIST_DIR}")
illumo_stage_app(@PROJECT_NAME@Package @PROJECT_ID@
  MODULE @PROJECT_NAME@.wasm
  MANIFEST "${_app}/illumo.json"
  FILES "${_app}/envvars.json")
unset(_app)
