# IllumoGame's WASM modules, included by IllumoGuest/CMakeLists.txt in the
# WASI guest build (one entry of ILLUMO_PROGRAMS). Paths are absolute: this
# file runs in IllumoGuest's directory scope.
set(_game "${CMAKE_CURRENT_LIST_DIR}")

# Shared product algorithms; only the execution scheduler differs from native.
# This target is a migration/parity target, not the shipping game entry point.
add_library(CSimGuestDomain STATIC
  "${_game}/Source/Game/SparseCellGrid.cpp"
  "${_game}/Source/Game/SparseWorkerPoolSerial.cpp"
  "${_game}/Source/Game/CellPattern.cpp"
  "${_game}/Source/Game/PatternCodec.cpp"
  "${_game}/Source/Game/BuiltinPatterns.cpp"
  "${_game}/Source/Game/IllumoCodecStreams.cpp"
  "${_game}/Source/Rulesets/RuleSet.cpp"
  "${_game}/Source/Rulesets/DataRuleSet.cpp"
  "${_game}/Source/Rulesets/RuleSetRegistry.cpp")
target_include_directories(CSimGuestDomain PUBLIC "${_game}/Source")
target_include_directories(CSimGuestDomain SYSTEM PRIVATE
  "${ILLUMO_ROOT}/Illumo/thirdparty/json/single_include"
  "${ILLUMO_ROOT}/Illumo/thirdparty/tracy-0.14.1/public")
target_link_libraries(CSimGuestDomain PUBLIC IllumoGuestOptions)

# IllumoGame itself: the complete product as a WASM package module. Native
# IllumoGame sources are shared with the native test oracle; only the
# platform adapter and serial runner are guest-specific.
illumo_add_guest(IllumoGame
  "${_game}/Source/Wasm/GameApplication.cpp"
  "${_game}/Source/Wasm/GuestPlatform.cpp"
  "${_game}/Source/Wasm/CatalogBootstrap.cpp"
  "${_game}/Source/Game/CanvasView.cpp"
  "${_game}/Source/Game/CellClipboard.cpp"
  "${_game}/Source/Game/CellContext.cpp"
  "${_game}/Source/Game/CanvasScene.cpp"
  "${_game}/Source/Game/CanvasActionBar.cpp"
  "${_game}/Source/Game/CanvasContextMenu.cpp"
  "${_game}/Source/Game/CanvasEditIcons.cpp"
  "${_game}/Source/Game/ConfigurationMenu.cpp"
  "${_game}/Source/Game/CSimScenes.cpp"
  "${_game}/Source/Game/CSimSounds.cpp"
  "${_game}/Source/Game/CSimTypeface.cpp"
  "${_game}/Source/Game/Cursor.cpp"
  "${_game}/Source/Game/ExitConfirmDialog.cpp"
  "${_game}/Source/Game/IllumoGameConfig.cpp"
  "${_game}/Source/Game/TitleScene.cpp"
  "${_game}/Source/Game/MenuMotifs.cpp"
  "${_game}/Source/Game/ModeBadge.cpp"
  "${_game}/Source/Game/SoftwareCursor.cpp"
  "${_game}/Source/Game/NewSimulationMenu.cpp"
  "${_game}/Source/Game/PerformanceOverlay.cpp"
  "${_game}/Source/Game/RuleCatalogOverlay.cpp"
  "${_game}/Source/Game/RulesetWorkshopMenu.cpp"
  "${_game}/Source/Game/SimulationRunnerGeneration.cpp"
  "${_game}/Source/Game/SimulatorSettings.cpp"
  "${_game}/Source/Wasm/SimulationLanes.cpp"
  "${_game}/Source/Wasm/SimulationRunnerGuest.cpp")
target_include_directories(IllumoGame SYSTEM PRIVATE
  "${ILLUMO_ROOT}/Illumo/thirdparty/json/single_include"
  "${ILLUMO_ROOT}/Illumo/thirdparty/tracy-0.14.1/public")
target_link_libraries(IllumoGame PRIVATE CSimGuestDomain IllumoGuestEngine IllumoGuestContent)

# The compute worker store: CSW1 whole-world jobs (parity tests) and CSL1
# simulation lanes (the game's generations).
add_executable(CSimWorkerGuest
  "${_game}/Source/Wasm/SimulationLanes.cpp"
  "${_game}/Source/Wasm/SimulationProtocol.cpp"
  "${_game}/Source/Wasm/SimulationWorkerExports.cpp")
set_target_properties(CSimWorkerGuest PROPERTIES SUFFIX ".wasm")
target_link_libraries(CSimWorkerGuest PRIVATE CSimGuestDomain)
target_include_directories(CSimWorkerGuest PRIVATE "${ILLUMO_ROOT}/Illumo/Include")
target_link_options(CSimWorkerGuest PRIVATE
  -Wl,--export=illumo_guest_describe -Wl,--export=illumo_guest_alloc
  -Wl,--export=illumo_guest_free -Wl,--export=illumo_guest_job
  -Wl,--export=illumo_guest_result_size)

add_executable(CSimDomainGuest "${_game}/Tests/Wasm/DomainGuest.cpp")
set_target_properties(CSimDomainGuest PROPERTIES SUFFIX ".wasm")
target_link_libraries(CSimDomainGuest PRIVATE CSimGuestDomain)
target_link_options(CSimDomainGuest PRIVATE
  -Wl,--export=domainInitialize -Wl,--export=domainCase
  -Wl,--export=domainAdvance -Wl,--export=domainHash
  -Wl,--export=domainSave -Wl,--export=domainSaveSize -Wl,--export=domainRestore
  -Wl,--export=allocate -Wl,--export=release)
unset(_game)
