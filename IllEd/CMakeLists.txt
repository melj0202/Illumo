cmake_minimum_required(VERSION 3.25)

add_library(IllEdCore STATIC
  Source/EditorAssetBrowser.cpp
  Source/EditorAssets.cpp
  Source/EditorClipboard.cpp
  Source/EditorConfirmDialog.cpp
  Source/EditorDocument.cpp
  Source/EditorGizmo.cpp
  Source/EditorHistory.cpp
  Source/EditorInspector.cpp
  Source/EditorModule.cpp
  Source/EditorModuleCommands.cpp
  Source/EditorModulePanels.cpp
  Source/EditorModuleViewport.cpp
  Source/EditorSceneGraphView.cpp
  Source/EditorSelection.cpp
  Source/EditorShortcuts.cpp
  Source/EditorToolbar.cpp
  Source/EditorToolsPanel.cpp
  Source/EditorUiAtlas.cpp
  Source/IllEdApplication.cpp
  Source/IllEdConfig.cpp
  Source/IllEdPlatformNative.cpp
)

function(illed_stage_assets target_name)
  add_custom_command(TARGET ${target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:${target_name}>/Assets/IllEd"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/Assets/editor-ui-atlas.jpg"
      "$<TARGET_FILE_DIR:${target_name}>/Assets/IllEd/editor-ui-atlas.jpg"
    VERBATIM)
endfunction()
target_include_directories(IllEdCore PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_include_directories(IllEdCore SYSTEM PRIVATE
  "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/tracy-0.13.1/public")
target_link_libraries(IllEdCore PUBLIC Illumo::Illumo Illumo::Content)
illumo_configure_runtime_target(IllEdCore)

# IllEd ships only as the WASM package apps/illed (cmake/IllumoWasm.cmake),
# played by IllumoRuntime. IllEdCore remains the native test oracle.

if(BUILD_TESTING)
  if(WIN32)
    # Explicit native-window regression; excluded from headless builds/tests.
    add_executable(IllEdCloseWindowTests EXCLUDE_FROM_ALL tools/CloseWindowTests.cpp)
    target_include_directories(IllEdCloseWindowTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Tests")
    target_link_libraries(IllEdCloseWindowTests PRIVATE IllEdCore glfw)
    illumo_configure_runtime_target(IllEdCloseWindowTests)
    illumo_stage_msvc_asan(IllEdCloseWindowTests)
    illed_stage_assets(IllEdCloseWindowTests)
  endif()
  add_executable(IllEdTests
    Tests/TestMain.cpp
    Tests/TestEditorAssets.cpp
    Tests/TestEditorClipboard.cpp
    Tests/TestEditorDocument.cpp
    Tests/TestEditorGizmo.cpp
    Tests/TestEditorHistory.cpp
    Tests/TestEditorInspector.cpp
    Tests/TestEditorModule.cpp
    Tests/TestEditorPanels.cpp
    Tests/TestEditorSceneGraphView.cpp
    Tests/TestEditorToolbar.cpp
    Tests/TestEditorToolsPanel.cpp
    Tests/TestEditorUiAtlas.cpp
    Tests/TestIllEdConfig.cpp
  )
  target_compile_definitions(IllEdTests PRIVATE ILLUMO_TESTING)
  target_include_directories(IllEdTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Tests")
  target_include_directories(IllEdTests SYSTEM PRIVATE
    "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/tracy-0.13.1/public")
  target_link_libraries(IllEdTests PRIVATE IllEdCore Illumo::TestSupport)
  illumo_configure_runtime_target(IllEdTests)
  illumo_discover_test_runner(IllEdTests IllEd)
  illumo_stage_msvc_asan(IllEdTests)
  illed_stage_assets(IllEdTests)
  illumo_stage_runtime(IllEdTests)
endif()
