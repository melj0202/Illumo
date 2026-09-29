# IllEd, the world editor, as a WASM package module; included by
# IllumoGuest/CMakeLists.txt in the WASI guest build (one entry of
# ILLUMO_PROGRAMS). Sources are shared with the native IllEdCore test oracle;
# only the platform adapter is guest code.
set(_illed "${CMAKE_CURRENT_LIST_DIR}/Source")
illumo_add_guest(IllEd
  "${_illed}/Wasm/EditorApplication.cpp"
  "${_illed}/EditorAssetBrowser.cpp"
  "${_illed}/EditorAssets.cpp"
  "${_illed}/EditorClipboard.cpp"
  "${_illed}/EditorConfirmDialog.cpp"
  "${_illed}/EditorDocument.cpp"
  "${_illed}/EditorGizmo.cpp"
  "${_illed}/EditorHistory.cpp"
  "${_illed}/EditorInspector.cpp"
  "${_illed}/EditorScene.cpp"
  "${_illed}/EditorSceneCommands.cpp"
  "${_illed}/EditorScenePanels.cpp"
  "${_illed}/EditorSceneViewport.cpp"
  "${_illed}/EditorSceneGraphView.cpp"
  "${_illed}/EditorSelection.cpp"
  "${_illed}/EditorShortcuts.cpp"
  "${_illed}/EditorToolbar.cpp"
  "${_illed}/EditorToolsPanel.cpp"
  "${_illed}/EditorUiAtlas.cpp"
  "${_illed}/IllEdConfig.cpp")
target_include_directories(IllEd PRIVATE "${_illed}")
target_include_directories(IllEd SYSTEM PRIVATE
  "${ILLUMO_ROOT}/Illumo/thirdparty/tracy-0.14.1/public")
target_link_libraries(IllEd PRIVATE IllumoGuestContent IllumoGuestEngine)
unset(_illed)
