cmake_minimum_required(VERSION 3.20)

add_library(IllEdCore STATIC
  Source/EditorAttachment.cpp
  Source/EditorConfirmDialog.cpp
  Source/EditorDocument.cpp
  Source/EditorModule.cpp
  Source/EditorSidebar.cpp
  Source/EditorToolbar.cpp
  Source/EditorUiAtlas.cpp
  Source/IllEdApplication.cpp
  Source/IllEdConfig.cpp
  Source/IlscCodec.cpp
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
  "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/json/single_include"
  "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/tracy-0.13.1/public")
target_link_libraries(IllEdCore PUBLIC Illumo::Illumo)
illumo_configure_runtime_target(IllEdCore)

add_executable(IllEd $<TARGET_OBJECTS:IllumoPlatformEntry>)
target_link_libraries(IllEd PRIVATE IllEdCore Illumo::Illumo)
illumo_configure_runtime_target(IllEd)
illumo_stage_runtime(IllEd)
illumo_stage_default_file(IllEd
  "${CMAKE_CURRENT_SOURCE_DIR}/envvars.json" envvars.json)
illumo_stage_msvc_asan(IllEd)
illed_stage_assets(IllEd)

if(BUILD_TESTING)
  add_executable(IllEdTests
    Tests/TestMain.cpp
    Tests/TestEditorDocument.cpp
    Tests/TestEditorModule.cpp
    Tests/TestEditorSidebar.cpp
    Tests/TestEditorToolbar.cpp
    Tests/TestEditorUiAtlas.cpp
    Tests/TestIllEdConfig.cpp
    Tests/TestIlscCodec.cpp
  )
  target_compile_definitions(IllEdTests PRIVATE ILLUMO_TESTING)
  target_include_directories(IllEdTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Tests")
  target_include_directories(IllEdTests SYSTEM PRIVATE
    "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/json/single_include"
    "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/tracy-0.13.1/public")
  target_link_libraries(IllEdTests PRIVATE IllEdCore Illumo::TestSupport)
  illumo_configure_runtime_target(IllEdTests)
  illumo_discover_test_runner(IllEdTests IllEd)
  illumo_stage_msvc_asan(IllEdTests)
  illed_stage_assets(IllEdTests)
  add_dependencies(IllEdTests IllEdRuntimeStage)
endif()
