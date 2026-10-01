# IllMeshViewer, the mesh viewer, as a WASM package module; included by
# IllumoGuest/CMakeLists.txt in the WASI guest build (one entry of
# ILLUMO_PROGRAMS). It shares its sources with the native IllMeshViewerCore
# test oracle.
set(_viewer "${CMAKE_CURRENT_LIST_DIR}/Source")
illumo_add_guest(IllMeshViewer
  "${_viewer}/Wasm/ViewerApplication.cpp"
  "${_viewer}/MeshViewerCamera.cpp"
  "${_viewer}/MeshViewerConfig.cpp"
  "${_viewer}/MeshViewerScene.cpp"
  "${_viewer}/MeshViewerScenePanels.cpp"
  "${_viewer}/MeshViewerPanels.cpp"
  "${_viewer}/MeshViewerUi.cpp")
target_include_directories(IllMeshViewer PRIVATE "${_viewer}")
target_include_directories(IllMeshViewer SYSTEM PRIVATE
  "${ILLUMO_ROOT}/Illumo/thirdparty/tracy-0.14.1/public")
target_link_libraries(IllMeshViewer PRIVATE IllumoGuestContent IllumoGuestEngine)
unset(_viewer)
