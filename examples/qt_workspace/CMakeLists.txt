include("${PROJECT_SOURCE_DIR}/cmake/QtPluginContract.cmake")
# Each actual plugin owns its metadata/moc; C++ and moc use the same state interface selection.
function(eve_add_notebook_plugin target output_subdir version state)
    set(directory "${CMAKE_CURRENT_BINARY_DIR}/plugin/${target}/$<CONFIG>")
    set(state_definition "EVE_NOTEBOOK_STATE=${state}")
    set(moc_definitions "-D${state_definition}")
    foreach(definition IN LISTS ARGN)
        list(APPEND moc_definitions "-D${definition}")
    endforeach()
    eve_qt_toolchain_json(toolchain)
    file(GENERATE OUTPUT "${directory}/notebook_metadata.json"
        CONTENT "{\"moduleId\":\"org.eve.example.notebook\",\"moduleVersion\":\"${version}\",\"plugins\":[\"example.notebook\"],\"toolchain\":${toolchain}}")
    set(moc_includes "-I${PROJECT_SOURCE_DIR}/sdk/include" "-I${directory}")
    foreach(include IN LISTS Qt5Core_INCLUDE_DIRS)
        list(APPEND moc_includes "-I${include}")
    endforeach()
    set(moc_dependencies notebook.cpp contract.hpp "${directory}/notebook_metadata.json"
        "${PROJECT_SOURCE_DIR}/sdk/include/eve/module/qt_plugin.hpp")
    if(state)
        list(APPEND moc_dependencies draft_state.hpp "${PROJECT_SOURCE_DIR}/sdk/include/eve/module/qt_state_plugin.hpp")
    endif()
    add_custom_command(OUTPUT "${directory}/notebook.moc"
        COMMAND Qt5::moc ${moc_definitions} ${moc_includes} "${CMAKE_CURRENT_SOURCE_DIR}/notebook.cpp"
            -o "${directory}/notebook.moc"
        DEPENDS ${moc_dependencies} VERBATIM)
    add_library(${target} MODULE notebook.cpp contract.hpp "${directory}/notebook.moc")
    if(state)
        target_sources(${target} PRIVATE draft_state.hpp)
    endif()
    target_include_directories(${target} PRIVATE "${directory}")
    target_compile_definitions(${target} PRIVATE "${state_definition}" ${ARGN})
    target_link_libraries(${target} PRIVATE Eve::QtUiPluginSdk)
    eve_target_defaults(${target})
    set_target_properties(${target} PROPERTIES PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/${output_subdir}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/${output_subdir}")
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${PROJECT_SOURCE_DIR}/cmake/WriteModuleManifest.cmake")
    add_custom_command(TARGET ${target} POST_BUILD
        BYPRODUCTS "${CMAKE_BINARY_DIR}/bin/${output_subdir}/module.json"
        COMMAND "${CMAKE_COMMAND}" "-DLIBRARY=$<TARGET_FILE:${target}>" "-DMODULE_ID=org.eve.example.notebook"
            "-DPLUGIN_ID=example.notebook" "-DMODULE_VERSION=${version}" -DLOADER=qt-ui -DUNLOAD_POLICY=resident
            -P "${PROJECT_SOURCE_DIR}/cmake/WriteModuleManifest.cmake" VERBATIM)
endfunction()
eve_add_notebook_plugin(eve_example_notebook qt-workspace/notebook 1.0.0 0)
eve_add_notebook_plugin(eve_example_notebook_a qt-upgrade/notebook-a 1.1.0 1)
eve_add_notebook_plugin(eve_example_notebook_b qt-upgrade/notebook-b 1.2.0 1)
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/bin/qt-workspace")
configure_file(workspace.yaml "${CMAKE_BINARY_DIR}/bin/qt-workspace/workspace.yaml" COPYONLY)
add_library(eve_notebook_host STATIC workspace.cpp bootstrap.cpp close.cpp modules.cpp
    modules_panel.cpp modules_panel_p.hpp diagnostics.cpp upgrade_panel.cpp upgrade_panel.hpp upgrade_text.cpp
    retirement.cpp retirement_p.hpp retirement_panel.cpp retirement_panel.hpp retirement_text.cpp workspace_p.hpp
    source.cpp source_p.hpp upgrade.cpp upgrade_execution.cpp upgrade_retirement.cpp upgrade_p.hpp)
target_link_libraries(eve_notebook_host PUBLIC Eve::ModuleQt Eve::ModuleQtPreflight Eve::ModuleInstall
    Eve::ConfigUi Eve::ConfigKernel Eve::ConfigStoreQt Eve::Replacement eve_native_service_modules)
target_link_libraries(eve_notebook_host PRIVATE Ela::WidgetTools)
target_include_directories(eve_notebook_host PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" PRIVATE "${PROJECT_SOURCE_DIR}/plugins/builtin/desktop-ela")
target_compile_definitions(eve_notebook_host PRIVATE EVE_PRODUCT_VERSION="${PROJECT_VERSION}")
eve_target_defaults(eve_notebook_host)
add_dependencies(eve_notebook_host eve_example_notebook)
if(EVE_BUILD_EXAMPLES)
    add_executable(eve_qt_workspace_demo main.cpp)
    target_link_libraries(eve_qt_workspace_demo PRIVATE eve_notebook_host)
    eve_target_defaults(eve_qt_workspace_demo)
    eve_attach_icon(eve_qt_workspace_demo)
endif()
if(BUILD_TESTING)
    eve_add_notebook_plugin(eve_example_notebook_fail_start qt-upgrade/notebook-fail-start 1.3.0 1
        EVE_EXAMPLE_NOTEBOOK_FAIL_START=1)
    add_executable(eve_notebook_state_tests
        "${PROJECT_SOURCE_DIR}/tests/qt/notebook_state_main.cpp"
        "${PROJECT_SOURCE_DIR}/tests/qt/notebook_state_scene.cpp"
        "${PROJECT_SOURCE_DIR}/tests/qt/notebook_state_runtime.cpp"
        "${PROJECT_SOURCE_DIR}/tests/qt/notebook_state_protocol.cpp")
    target_link_libraries(eve_notebook_state_tests PRIVATE
        Eve::ModuleQt Eve::ModuleQtPreflight eve_desktop_impl Qt5::Test Ela::WidgetTools)
    set_target_properties(eve_notebook_state_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    eve_target_defaults(eve_notebook_state_tests)
    add_dependencies(eve_notebook_state_tests eve_example_notebook eve_example_notebook_a eve_example_notebook_b)
    foreach(case IN ITEMS protocol_a protocol_b draft_a draft_b draft_empty draft_limits draft_multi draft_frozen
        draft_rejections draft_start_failure legacy)
        add_test(NAME p5.notebook_state_${case} COMMAND eve_notebook_state_tests "${CMAKE_BINARY_DIR}/bin" "${case}")
        set_tests_properties(p5.notebook_state_${case} PROPERTIES LABELS "p5;module;qt;ui;state;notebook_state" TIMEOUT 30
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
    add_executable(eve_qt_workspace_tests "${PROJECT_SOURCE_DIR}/tests/qt/module_qt_workspace.cpp")
    target_link_libraries(eve_qt_workspace_tests PRIVATE eve_notebook_host)
    eve_target_defaults(eve_qt_workspace_tests)
    foreach(case IN ITEMS normal multi update toggle add save start_failure cleanup_failure diagnostics guards close_conflict output_limit)
        add_test(NAME p4.qt_workspace_${case} COMMAND eve_qt_workspace_tests "${CMAKE_BINARY_DIR}/bin/qt-workspace" "${case}")
        set_tests_properties(p4.qt_workspace_${case} PROPERTIES LABELS "p4;module;qt;ui;integration" TIMEOUT 25
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
    add_executable(eve_platform_workspace_tests
        "${PROJECT_SOURCE_DIR}/tests/qt/platform_workspace_main.cpp"
        "${PROJECT_SOURCE_DIR}/tests/qt/platform_workspace_runtime.cpp"
        "${PROJECT_SOURCE_DIR}/tests/qt/platform_workspace_close.cpp")
    add_library(eve_platform_workspace_support STATIC "${PROJECT_SOURCE_DIR}/tests/qt/platform_workspace_scene.cpp")
    target_link_libraries(eve_platform_workspace_support PUBLIC eve_notebook_host Ela::WidgetTools)
    eve_target_defaults(eve_platform_workspace_support)
    target_link_libraries(eve_platform_workspace_tests PRIVATE eve_platform_workspace_support)
    set_target_properties(eve_platform_workspace_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    eve_target_defaults(eve_platform_workspace_tests)
    add_dependencies(eve_platform_workspace_tests eve_example_counter_a eve_example_counter_b
        eve_example_reporter_a eve_example_reporter_b eve_example_notebook_a eve_example_notebook_b)
    foreach(case IN ITEMS normal multi existing_current save_conflict open_failure partial_open_failure
        retained_runtime discarded_close cleanup_failure)
        add_test(NAME p5.platform_workspace_${case} COMMAND eve_platform_workspace_tests "${CMAKE_BINARY_DIR}/bin" "${case}")
        set_tests_properties(p5.platform_workspace_${case} PROPERTIES LABELS "p5;module;qt;ui;integration;platform_workspace"
            TIMEOUT 45 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
    add_library(eve_platform_upgrade_support STATIC "${PROJECT_SOURCE_DIR}/tests/qt/platform_upgrade_support.cpp")
    target_link_libraries(eve_platform_upgrade_support PUBLIC eve_platform_workspace_support)
    eve_target_defaults(eve_platform_upgrade_support)
    add_executable(eve_platform_upgrade_tests
        "${PROJECT_SOURCE_DIR}/tests/qt/platform_upgrade_main.cpp"
        "${PROJECT_SOURCE_DIR}/tests/qt/platform_upgrade_native.cpp"
        "${PROJECT_SOURCE_DIR}/tests/qt/platform_upgrade_qt.cpp"
        "${PROJECT_SOURCE_DIR}/tests/qt/platform_upgrade_contract.cpp")
    target_link_libraries(eve_platform_upgrade_tests PRIVATE eve_platform_upgrade_support)
    set_target_properties(eve_platform_upgrade_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    eve_target_defaults(eve_platform_upgrade_tests)
    add_dependencies(eve_platform_upgrade_tests eve_example_counter_a eve_example_counter_b
        eve_example_reporter_a eve_example_reporter_b eve_example_notebook_a eve_example_notebook_b
        eve_example_notebook_fail_start)
    foreach(case IN ITEMS counter reporter native_roundtrip retained_factory notebook notebook_restore held_protection
        committed_discard admission stale_revision changed_source close_inflight)
        add_test(NAME p5.platform_upgrade_${case} COMMAND eve_platform_upgrade_tests "${CMAKE_BINARY_DIR}/bin" "${case}")
        set_tests_properties(p5.platform_upgrade_${case} PROPERTIES LABELS "platform_upgrade;p5;module;qt;ui;replacement"
            TIMEOUT 45 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
    if(WIN32)
        add_library(eve_workspace_directory_support STATIC
            "${PROJECT_SOURCE_DIR}/tests/qt/workspace_directory.cpp")
        target_link_libraries(eve_workspace_directory_support PUBLIC eve_platform_upgrade_support)
        eve_target_defaults(eve_workspace_directory_support)
        add_executable(eve_workspace_endurance_tests
            "${PROJECT_SOURCE_DIR}/tests/qt/workspace_endurance_main.cpp"
            "${PROJECT_SOURCE_DIR}/tests/qt/workspace_endurance_round.cpp"
            "${PROJECT_SOURCE_DIR}/tests/qt/workspace_endurance_catalog.cpp"
            "${PROJECT_SOURCE_DIR}/tests/qt/workspace_endurance_windows.cpp")
        target_link_libraries(eve_workspace_endurance_tests PRIVATE eve_workspace_directory_support user32)
        set_target_properties(eve_workspace_endurance_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        eve_target_defaults(eve_workspace_endurance_tests)
        add_dependencies(eve_workspace_endurance_tests eve_example_counter_a eve_example_counter_b
            eve_example_reporter_a eve_example_reporter_b eve_example_notebook_a eve_example_notebook_b)
        add_test(NAME p5.workspace_endurance_native COMMAND eve_workspace_endurance_tests
            "${CMAKE_BINARY_DIR}/bin" "${CMAKE_BINARY_DIR}/p5-m5d1/endurance")
        set_tests_properties(p5.workspace_endurance_native PROPERTIES
            LABELS "p5;module;qt;ui;replacement;endurance;native_windows" TIMEOUT 900
            ENVIRONMENT "QT_QPA_PLATFORM=windows")
        add_executable(eve_workspace_stress_tests
            "${PROJECT_SOURCE_DIR}/tests/qt/workspace_stress_main.cpp"
            "${PROJECT_SOURCE_DIR}/tests/qt/workspace_stress_generate.cpp"
            "${PROJECT_SOURCE_DIR}/tests/qt/workspace_stress_trace.cpp"
            "${PROJECT_SOURCE_DIR}/tests/qt/workspace_stress_child.cpp"
            "${PROJECT_SOURCE_DIR}/tests/qt/workspace_stress_replay.cpp"
            "${PROJECT_SOURCE_DIR}/tests/qt/workspace_stress_invalid.cpp"
            "${PROJECT_SOURCE_DIR}/tests/qt/workspace_stress_reduce.cpp"
            "${PROJECT_SOURCE_DIR}/tests/qt/workspace_stress_actions.cpp"
            "${PROJECT_SOURCE_DIR}/tests/qt/workspace_stress_upgrade.cpp"
            "${PROJECT_SOURCE_DIR}/tests/qt/workspace_stress_observe.cpp")
        target_link_libraries(eve_workspace_stress_tests PRIVATE eve_workspace_directory_support)
        set_target_properties(eve_workspace_stress_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        eve_target_defaults(eve_workspace_stress_tests)
        add_dependencies(eve_workspace_stress_tests eve_example_counter_a eve_example_counter_b
            eve_example_reporter_a eve_example_reporter_b eve_example_notebook_a eve_example_notebook_b)
        foreach(seed IN ITEMS 1 18446744073709551615)
            add_test(NAME p5.workspace_stress_seed_${seed} COMMAND eve_workspace_stress_tests generate
                "${CMAKE_BINARY_DIR}/bin" "${seed}" "${CMAKE_BINARY_DIR}/p5-m5d2a/seed-${seed}")
            set_tests_properties(p5.workspace_stress_seed_${seed} PROPERTIES
                LABELS "p5;module;qt;ui;replacement;stress;native_windows" TIMEOUT 180
                ENVIRONMENT "QT_QPA_PLATFORM=windows")
        endforeach()
        foreach(proof IN ITEMS replay invalid reduce)
            add_test(NAME p5.workspace_stress_${proof}_proof COMMAND eve_workspace_stress_tests "${proof}-proof"
                "${CMAKE_BINARY_DIR}/bin" "${CMAKE_BINARY_DIR}/p5-m5d2b/${proof}")
            set_tests_properties(p5.workspace_stress_${proof}_proof PROPERTIES
                LABELS "p5;module;qt;ui;replacement;stress;native_windows" TIMEOUT 900
                ENVIRONMENT "QT_QPA_PLATFORM=windows")
        endforeach()
    endif()
    add_executable(eve_modules_view_tests
        "${PROJECT_SOURCE_DIR}/tests/qt/modules_view.cpp"
        "${PROJECT_SOURCE_DIR}/tests/qt/owned_appearance.cpp"
        "${PROJECT_SOURCE_DIR}/tests/qt/modules_view_close.cpp")
    target_link_libraries(eve_modules_view_tests PRIVATE eve_platform_workspace_support Qt5::Test)
    set_target_properties(eve_modules_view_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    eve_target_defaults(eve_modules_view_tests)
    add_dependencies(eve_modules_view_tests eve_example_counter_a eve_example_counter_b
        eve_example_reporter_a eve_example_reporter_b eve_example_notebook_a eve_example_notebook_b)
    foreach(case IN ITEMS menu catalogue legacy view_close save_conflict view_discard guards)
        add_test(NAME p5.modules_view_${case} COMMAND eve_modules_view_tests "${CMAKE_BINARY_DIR}/bin" "${case}")
        set_tests_properties(p5.modules_view_${case} PROPERTIES LABELS "modules_view;p5;module;qt;ui;integration"
            TIMEOUT 45 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
    add_executable(eve_upgrade_view_tests
        "${PROJECT_SOURCE_DIR}/tests/qt/upgrade_view_main.cpp"
        "${PROJECT_SOURCE_DIR}/tests/qt/upgrade_view_controls.cpp"
        "${PROJECT_SOURCE_DIR}/tests/qt/upgrade_view_operations.cpp"
        "${PROJECT_SOURCE_DIR}/tests/qt/upgrade_view_failures.cpp"
        "${PROJECT_SOURCE_DIR}/tests/qt/upgrade_fault_state.cpp"
        "${PROJECT_SOURCE_DIR}/tests/qt/upgrade_fault_stop.cpp"
        "${PROJECT_SOURCE_DIR}/tests/qt/upgrade_fault_native.cpp"
        "${PROJECT_SOURCE_DIR}/tests/qt/upgrade_fault_s09.cpp"
        "${PROJECT_SOURCE_DIR}/tests/qt/upgrade_fault_retirement.cpp")
    target_link_libraries(eve_upgrade_view_tests PRIVATE eve_platform_upgrade_support)
    set_target_properties(eve_upgrade_view_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    eve_target_defaults(eve_upgrade_view_tests)
    add_dependencies(eve_upgrade_view_tests eve_example_counter_a eve_example_counter_b
        eve_example_reporter_a eve_example_reporter_b eve_example_notebook_a eve_example_notebook_b
        eve_example_notebook_fail_start eve_example_counter_missing_query eve_example_counter_missing_business
        eve_example_reporter_fail_once eve_example_reporter_fail_recovery)
    foreach(case IN ITEMS preview picker_close invalidation changed_source counter notebook cancel_prepared
        cancel_building save_conflict committed_discard)
        add_test(NAME p5.upgrade_view_${case} COMMAND eve_upgrade_view_tests "${CMAKE_BINARY_DIR}/bin" "${case}")
        set_tests_properties(p5.upgrade_view_${case} PROPERTIES LABELS "upgrade_view;p5;module;qt;ui;replacement"
            TIMEOUT 45 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
    foreach(case IN ITEMS notebook_restore draft_limit stop_counter stop_notebook missing_query missing_business
        s09_restored s09_recovery_failed retirement)
        add_test(NAME p5.upgrade_fault_${case} COMMAND eve_upgrade_view_tests "${CMAKE_BINARY_DIR}/bin" "fault_${case}")
        set_tests_properties(p5.upgrade_fault_${case} PROPERTIES LABELS "upgrade_fault;p5;module;qt;ui;replacement"
            TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
endif()
