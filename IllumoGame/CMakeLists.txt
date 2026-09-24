cmake_minimum_required(VERSION 3.25)

add_library(IllumoGameCore STATIC
  Source/Game/BuiltinPatterns.cpp
  Source/Game/CanvasView.cpp
  Source/Game/CellClipboard.cpp
  Source/Game/CellClipboardSystem.cpp
  Source/Game/CSimPlatformNative.cpp
  Source/Game/CellContext.cpp
  Source/Game/CellGameModule.cpp
  Source/Game/CellPattern.cpp
  Source/Game/ConfigurationMenu.cpp
  Source/Game/Cursor.cpp
  Source/Game/ExitConfirmDialog.cpp
  Source/Game/IllumoCodec.cpp
  Source/Game/IllumoCodecStreams.cpp
  Source/Game/IllumoGameApplication.cpp
  Source/Game/IllumoGameConfig.cpp
  Source/Game/MainMenuModule.cpp
  Source/Game/MenuMotifs.cpp
  Source/Game/ModeBadge.cpp
  Source/Game/NewSimulationMenu.cpp
  Source/Game/PatternCodec.cpp
  Source/Game/RuleCatalogLoader.cpp
  Source/Game/RuleCatalogOverlay.cpp
  Source/Game/RulesetWorkshopMenu.cpp
  Source/Game/SimulationRunner.cpp
  Source/Game/SimulationRunnerGeneration.cpp
  Source/Game/SparseCellGrid.cpp
  Source/Game/SparseWorkerPool.cpp
  Source/Rulesets/DataRuleSet.cpp
  Source/Rulesets/RuleSet.cpp
  Source/Rulesets/RuleSetRegistry.cpp
)
target_include_directories(IllumoGameCore PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_include_directories(IllumoGameCore SYSTEM PRIVATE
  "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/json/single_include"
  "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/tracy-0.13.1/public")
target_link_libraries(IllumoGameCore PUBLIC Illumo::Illumo Illumo::Content)
illumo_configure_runtime_target(IllumoGameCore)

# IllumoGame ships only as the IllumoGame.wasm package hosted by IllumoRuntime
# (cmake/IllumoWasm.cmake). IllumoGameCore remains the native test oracle for
# the same product sources; no native game executable is built.

if(BUILD_TESTING)
  add_executable(IllumoGameTests
    Tests/DenseRuleEvaluator.cpp
    Source/Game/Canvas.cpp
    Source/Game/CellGrid.cpp
    Source/Rulesets/BriansBrainRuleSet.cpp
    Source/Rulesets/Elementary1DRuleSet.cpp
    Source/Rulesets/LifeLikeRuleSet.cpp
    Source/Rulesets/WireworldRuleSet.cpp
    Tests/TestMain.cpp
    Tests/TestRuleSets.cpp
    Tests/TestRuleCatalogLoader.cpp
    Tests/TestCellContext.cpp
    Tests/TestCanvasDomain.cpp
    Tests/TestDomainBoundary.cpp
    Tests/TestSim.cpp
    Tests/TestCellGameModule.cpp
    Tests/TestMainMenuModule.cpp
    Tests/TestProductBoundary.cpp
    Tests/TestCanvasInf.cpp
    Tests/TestWorldTopology.cpp
    Tests/TestConfigurationMenu.cpp
    Tests/TestExitConfirmDialog.cpp
    Tests/TestIllumoGameConfig.cpp
    Tests/TestEditor.cpp
  )
  target_compile_definitions(IllumoGameTests PRIVATE ILLUMO_TESTING)
  target_include_directories(IllumoGameTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Tests")
  target_include_directories(IllumoGameTests SYSTEM PRIVATE
    "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/tracy-0.13.1/public")
  target_link_libraries(IllumoGameTests PRIVATE
    IllumoGameCore Illumo::TestSupport)
  illumo_configure_runtime_target(IllumoGameTests)
  illumo_discover_test_runner(IllumoGameTests IllumoGame)
  illumo_stage_msvc_asan(IllumoGameTests)
  # The oracle reads shipped catalogs beside its executable, as the former
  # native game did.
  illumo_stage_runtime(IllumoGameTests)
  illumo_stage_runtime_file(IllumoGameTests
    "${CMAKE_CURRENT_SOURCE_DIR}/rulesets.json" rulesets.json)
  illumo_stage_runtime_file(IllumoGameTests
    "${CMAKE_CURRENT_SOURCE_DIR}/families.json" families.json)
  # render3dTest reads its scene through the asset source (relative to the
  # working directory natively).
  add_custom_command(TARGET IllumoGameTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:IllumoGameTests>/Scenes"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/Scenes/render3d-test.ilsc"
      "$<TARGET_FILE_DIR:IllumoGameTests>/Scenes/render3d-test.ilsc"
    VERBATIM)
endif()
