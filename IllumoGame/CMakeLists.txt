cmake_minimum_required(VERSION 3.20)

add_library(IllumoGameCore STATIC
  Source/Game/BuiltinPatterns.cpp
  Source/Game/Canvas.cpp
  Source/Game/CanvasView.cpp
  Source/Game/CellGameModule.cpp
  Source/Game/CellGrid.cpp
  Source/Game/CellPattern.cpp
  Source/Game/ConfigurationMenu.cpp
  Source/Game/Cursor.cpp
  Source/Game/ExitConfirmDialog.cpp
  Source/Game/IllumoGameApplication.cpp
  Source/Game/IllumoGameConfig.cpp
  Source/Game/PatternCodec.cpp
  Source/Game/SimulationRunner.cpp
  Source/Game/SparseCellGrid.cpp
  Source/Rulesets/BriansBrainRuleSet.cpp
  Source/Rulesets/DayAndNightRuleSet.cpp
  Source/Rulesets/GameOfLifeRuleSet.cpp
  Source/Rulesets/HighlifeRuleSet.cpp
  Source/Rulesets/LifeWithoutDeathRuleSet.cpp
  Source/Rulesets/Rule184RuleSet.cpp
  Source/Rulesets/Rule90RuleSet.cpp
  Source/Rulesets/RuleSet.cpp
  Source/Rulesets/SeedsRuleSet.cpp
  Source/Rulesets/WireworldRuleSet.cpp
)
target_include_directories(IllumoGameCore PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_include_directories(IllumoGameCore SYSTEM PRIVATE
  "${ILLUMO_LIBRARY_SOURCE_DIR}/thirdparty/tracy-0.13.1/public")
target_link_libraries(IllumoGameCore PUBLIC Illumo::Illumo)
illumo_configure_runtime_target(IllumoGameCore)

add_executable(IllumoGame $<TARGET_OBJECTS:IllumoPlatformEntry>)
target_link_libraries(IllumoGame PRIVATE IllumoGameCore Illumo::Illumo)
illumo_configure_runtime_target(IllumoGame)
illumo_stage_runtime(IllumoGame)
illumo_stage_default_file(IllumoGame
  "${CMAKE_CURRENT_SOURCE_DIR}/envvars.json" envvars.json)
illumo_stage_msvc_asan(IllumoGame)

if(BUILD_TESTING)
  add_executable(IllumoGameTests
    Tests/TestMain.cpp
    Tests/TestRuleSets.cpp
    Tests/TestCellContext.cpp
    Tests/TestCanvasDomain.cpp
    Tests/TestDomainBoundary.cpp
    Tests/TestSim.cpp
    Tests/TestCellGameModule.cpp
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
  add_dependencies(IllumoGameTests IllumoGameRuntimeStage)
endif()
