include("${PROJECT_SOURCE_DIR}/cmake/QtPluginContract.cmake")
# Each actual plugin owns its metadata/moc; C++ and moc use the same state interface selection.
function(cordis_add_notebook_plugin target output_subdir version state)
    set(directory "${CMAKE_CURRENT_BINARY_DIR}/plugin/${target}/$<CONFIG>")
    set(state_definition "CORDIS_NOTEBOOK_STATE=${state}")
    set(moc_definitions "-D${state_definition}")
    foreach(definition IN LISTS ARGN)
        list(APPEND moc_definitions "-D${definition}")
    endforeach()
    cordis_qt_toolchain_json(toolchain)
    file(GENERATE OUTPUT "${directory}/notebook_metadata.json"
        CONTENT "{\"moduleId\":\"org.cordis.example.notebook\",\"moduleVersion\":\"${version}\",\"plugins\":[\"example.notebook\"],\"toolchain\":${toolchain}}")
    set(moc_includes "-I${PROJECT_SOURCE_DIR}/sdk/include" "-I${directory}")
    foreach(include IN LISTS Qt5Core_INCLUDE_DIRS)
        list(APPEND moc_includes "-I${include}")
    endforeach()
    set(moc_dependencies notebook.cpp contract.hpp "${directory}/notebook_metadata.json"
        "${PROJECT_SOURCE_DIR}/sdk/include/cordis/module/qt_plugin.hpp")
    if(state)
        list(APPEND moc_dependencies draft_state.hpp "${PROJECT_SOURCE_DIR}/sdk/include/cordis/module/qt_state_plugin.hpp")
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
    target_link_libraries(${target} PRIVATE Cordis::QtUiPluginSdk)
    cordis_target_defaults(${target})
    set_target_properties(${target} PROPERTIES PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/${output_subdir}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/${output_subdir}")
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${PROJECT_SOURCE_DIR}/cmake/WriteModuleManifest.cmake")
    add_custom_command(TARGET ${target} POST_BUILD
        BYPRODUCTS "${CMAKE_BINARY_DIR}/bin/${output_subdir}/module.json"
        COMMAND "${CMAKE_COMMAND}" "-DLIBRARY=$<TARGET_FILE:${target}>" "-DMODULE_ID=org.cordis.example.notebook"
            "-DPLUGIN_ID=example.notebook" "-DMODULE_VERSION=${version}" -DLOADER=qt-ui -DUNLOAD_POLICY=resident
            -P "${PROJECT_SOURCE_DIR}/cmake/WriteModuleManifest.cmake" VERBATIM)
endfunction()
cordis_add_notebook_plugin(cordis_example_notebook qt-workspace/notebook 1.0.0 0)
cordis_add_notebook_plugin(cordis_example_notebook_a qt-upgrade/notebook-a 1.1.0 1)
cordis_add_notebook_plugin(cordis_example_notebook_b qt-upgrade/notebook-b 1.2.0 1)
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/bin/qt-workspace")
configure_file(workspace.yaml "${CMAKE_BINARY_DIR}/bin/qt-workspace/workspace.yaml" COPYONLY)
add_library(cordis_notebook_host STATIC workspace.cpp bootstrap.cpp close.cpp modules.cpp
    modules_panel.cpp modules_panel_p.hpp diagnostics.cpp workspace_p.hpp
    source.cpp source_p.hpp upgrade.cpp upgrade_execution.cpp upgrade_retirement.cpp upgrade_p.hpp)
target_link_libraries(cordis_notebook_host PUBLIC Cordis::ModuleQt Cordis::ModuleQtPreflight Cordis::ModuleInstall
    Cordis::ConfigUi Cordis::ConfigKernel Cordis::ConfigStoreQt Cordis::Replacement cordis_native_service_modules)
target_link_libraries(cordis_notebook_host PRIVATE Ela::WidgetTools)
target_include_directories(cordis_notebook_host PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
cordis_target_defaults(cordis_notebook_host)
add_dependencies(cordis_notebook_host cordis_example_notebook)
if(CORDIS_BUILD_EXAMPLES)
    add_executable(cordis_qt_workspace_demo main.cpp)
    target_link_libraries(cordis_qt_workspace_demo PRIVATE cordis_notebook_host)
    cordis_target_defaults(cordis_qt_workspace_demo)
    cordis_attach_eve_icon(cordis_qt_workspace_demo)
    add_executable(cordis_platform_workbench_demo main.cpp)
    target_compile_definitions(cordis_platform_workbench_demo PRIVATE CORDIS_PLATFORM_QUALIFICATION=1)
    target_link_libraries(cordis_platform_workbench_demo PRIVATE cordis_notebook_host)
    cordis_target_defaults(cordis_platform_workbench_demo)
    cordis_attach_eve_icon(cordis_platform_workbench_demo)
    add_dependencies(cordis_platform_workbench_demo cordis_example_counter_a cordis_example_counter_b
        cordis_example_reporter_a cordis_example_reporter_b cordis_example_notebook_a cordis_example_notebook_b)
endif()
if(BUILD_TESTING)
    cordis_add_notebook_plugin(cordis_example_notebook_fail_start qt-upgrade/notebook-fail-start 1.3.0 1
        CORDIS_EXAMPLE_NOTEBOOK_FAIL_START=1)
    add_executable(cordis_notebook_state_tests
        "${PROJECT_SOURCE_DIR}/tests/qt/notebook_state_main.cpp"
        "${PROJECT_SOURCE_DIR}/tests/qt/notebook_state_scene.cpp"
        "${PROJECT_SOURCE_DIR}/tests/qt/notebook_state_runtime.cpp"
        "${PROJECT_SOURCE_DIR}/tests/qt/notebook_state_protocol.cpp")
    target_link_libraries(cordis_notebook_state_tests PRIVATE
        Cordis::ModuleQt Cordis::ModuleQtPreflight Cordis::Ui Qt5::Test Ela::WidgetTools)
    set_target_properties(cordis_notebook_state_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    cordis_target_defaults(cordis_notebook_state_tests)
    add_dependencies(cordis_notebook_state_tests cordis_example_notebook cordis_example_notebook_a cordis_example_notebook_b)
    foreach(case IN ITEMS protocol_a protocol_b draft_a draft_b draft_empty draft_limits draft_multi draft_frozen
        draft_rejections draft_start_failure legacy)
        add_test(NAME p5.notebook_state_${case} COMMAND cordis_notebook_state_tests "${CMAKE_BINARY_DIR}/bin" "${case}")
        set_tests_properties(p5.notebook_state_${case} PROPERTIES LABELS "p5;module;qt;ui;state;notebook_state" TIMEOUT 30
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
    add_executable(cordis_qt_workspace_tests "${PROJECT_SOURCE_DIR}/tests/qt/module_qt_workspace.cpp")
    target_link_libraries(cordis_qt_workspace_tests PRIVATE cordis_notebook_host)
    cordis_target_defaults(cordis_qt_workspace_tests)
    foreach(case IN ITEMS normal multi update toggle add save start_failure cleanup_failure diagnostics guards close_conflict output_limit)
        add_test(NAME p4.qt_workspace_${case} COMMAND cordis_qt_workspace_tests "${CMAKE_BINARY_DIR}/bin/qt-workspace" "${case}")
        set_tests_properties(p4.qt_workspace_${case} PROPERTIES LABELS "p4;module;qt;ui;integration" TIMEOUT 25
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
    add_executable(cordis_platform_workspace_tests
        "${PROJECT_SOURCE_DIR}/tests/qt/platform_workspace_main.cpp"
        "${PROJECT_SOURCE_DIR}/tests/qt/platform_workspace_runtime.cpp"
        "${PROJECT_SOURCE_DIR}/tests/qt/platform_workspace_close.cpp")
    add_library(cordis_platform_workspace_support STATIC "${PROJECT_SOURCE_DIR}/tests/qt/platform_workspace_scene.cpp")
    target_link_libraries(cordis_platform_workspace_support PUBLIC cordis_notebook_host Ela::WidgetTools)
    cordis_target_defaults(cordis_platform_workspace_support)
    target_link_libraries(cordis_platform_workspace_tests PRIVATE cordis_platform_workspace_support)
    set_target_properties(cordis_platform_workspace_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    cordis_target_defaults(cordis_platform_workspace_tests)
    add_dependencies(cordis_platform_workspace_tests cordis_example_counter_a cordis_example_counter_b
        cordis_example_reporter_a cordis_example_reporter_b cordis_example_notebook_a cordis_example_notebook_b)
    foreach(case IN ITEMS normal multi existing_current save_conflict open_failure partial_open_failure
        retained_runtime discarded_close cleanup_failure)
        add_test(NAME p5.platform_workspace_${case} COMMAND cordis_platform_workspace_tests "${CMAKE_BINARY_DIR}/bin" "${case}")
        set_tests_properties(p5.platform_workspace_${case} PROPERTIES LABELS "p5;module;qt;ui;integration;platform_workspace"
            TIMEOUT 45 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
    add_executable(cordis_platform_upgrade_tests
        "${PROJECT_SOURCE_DIR}/tests/qt/platform_upgrade_main.cpp"
        "${PROJECT_SOURCE_DIR}/tests/qt/platform_upgrade_support.cpp"
        "${PROJECT_SOURCE_DIR}/tests/qt/platform_upgrade_native.cpp"
        "${PROJECT_SOURCE_DIR}/tests/qt/platform_upgrade_qt.cpp"
        "${PROJECT_SOURCE_DIR}/tests/qt/platform_upgrade_contract.cpp")
    target_link_libraries(cordis_platform_upgrade_tests PRIVATE cordis_platform_workspace_support)
    set_target_properties(cordis_platform_upgrade_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    cordis_target_defaults(cordis_platform_upgrade_tests)
    add_dependencies(cordis_platform_upgrade_tests cordis_example_counter_a cordis_example_counter_b
        cordis_example_reporter_a cordis_example_reporter_b cordis_example_notebook_a cordis_example_notebook_b
        cordis_example_notebook_fail_start)
    foreach(case IN ITEMS counter reporter native_roundtrip retained_factory notebook notebook_restore held_protection
        committed_discard admission stale_revision changed_source close_inflight)
        add_test(NAME p5.platform_upgrade_${case} COMMAND cordis_platform_upgrade_tests "${CMAKE_BINARY_DIR}/bin" "${case}")
        set_tests_properties(p5.platform_upgrade_${case} PROPERTIES LABELS "platform_upgrade;p5;module;qt;ui;replacement"
            TIMEOUT 45 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
    add_executable(cordis_modules_view_tests
        "${PROJECT_SOURCE_DIR}/tests/qt/modules_view.cpp"
        "${PROJECT_SOURCE_DIR}/tests/qt/modules_view_close.cpp")
    target_link_libraries(cordis_modules_view_tests PRIVATE cordis_platform_workspace_support)
    set_target_properties(cordis_modules_view_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    cordis_target_defaults(cordis_modules_view_tests)
    add_dependencies(cordis_modules_view_tests cordis_example_counter_a cordis_example_counter_b
        cordis_example_reporter_a cordis_example_reporter_b cordis_example_notebook_a cordis_example_notebook_b)
    foreach(case IN ITEMS menu catalogue legacy view_close save_conflict view_discard guards)
        add_test(NAME p5.modules_view_${case} COMMAND cordis_modules_view_tests "${CMAKE_BINARY_DIR}/bin" "${case}")
        set_tests_properties(p5.modules_view_${case} PROPERTIES LABELS "modules_view;p5;module;qt;ui;integration"
            TIMEOUT 45 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
endif()
