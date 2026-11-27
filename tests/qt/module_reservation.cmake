add_library(eve_reservation_native_next MODULE "${PROJECT_SOURCE_DIR}/tests/fixtures/module_fixture.c"
    "${PROJECT_SOURCE_DIR}/tests/fixtures/business_fixture.c")
eve_abi_test_defaults(eve_reservation_native_next)
target_compile_definitions(eve_reservation_native_next PRIVATE EVE_MODULE_EXPORTS EVE_ABI_EXPORTS
    FIXTURE_ID="org.eve.business" FIXTURE_VERSION="2.0.0")
set_target_properties(eve_reservation_native_next PROPERTIES PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/replacement-fixtures")
foreach(version IN ITEMS old next)
    add_library(eve_reservation_peer_${version} MODULE "${PROJECT_SOURCE_DIR}/tests/fixtures/module_fixture.c"
        "${PROJECT_SOURCE_DIR}/tests/fixtures/business_fixture.c")
    eve_abi_test_defaults(eve_reservation_peer_${version})
    target_compile_definitions(eve_reservation_peer_${version} PRIVATE EVE_MODULE_EXPORTS EVE_ABI_EXPORTS
        FIXTURE_ID="org.eve.peer" FIXTURE_PLUGIN_ID="org.eve.peer.plugin"
        FIXTURE_VERSION="$<IF:$<STREQUAL:${version},old>,1.0.0,2.0.0>")
    set_target_properties(eve_reservation_peer_${version} PROPERTIES PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/replacement-fixtures")
endforeach()
add_executable(eve_module_reservation_tests qt/module_reservation.cpp qt/module_state_tree.cpp)
target_link_libraries(eve_module_reservation_tests PRIVATE Eve::ModuleNative Eve::ModuleHost Eve::ConfigKernel Eve::ConfigSession Eve::ConfigStoreQt Qt5::Core)
eve_target_defaults(eve_module_reservation_tests)
add_dependencies(eve_module_reservation_tests eve_fixture_business eve_reservation_native_next
    eve_reservation_peer_old eve_reservation_peer_next eve_candidate_native_next eve_fixture_services)
target_link_libraries(eve_module_reservation_tests PRIVATE Eve::ConfigReplacement Eve::Replacement)
add_executable(eve_module_origin_tests qt/module_origin.cpp)
target_link_libraries(eve_module_origin_tests PRIVATE Eve::ModuleNative Eve::ModuleHost Qt5::Core)
eve_target_defaults(eve_module_origin_tests)
add_dependencies(eve_module_origin_tests eve_fixture_business eve_reservation_native_next)
function(eve_reservation_cases backend old next third)
    foreach(operation IN ITEMS
        operation_state_tree_roundtrip
        operation_state_tree_recovery
        operation_state_tree_recovery_consumer
        operation_state_tree_delete
        operation_state_tree_delete_recovery
        operation_state_tree_legacy
        operation_state_tree_mixed
        operation_state_tree_budget_boundary
        operation_state_tree_budget_full
        operation_state_tree_budget_over
        operation_state_tree_source
        operation_state_tree_source_recovery
        operation_state_tree_removed_parent
        operation_state_tree_removed_new_parent
        operation_state_tree_removed_config
        operation_sources_raw_loss
        operation_sources_loss
        operation_sources_loss_shared
        operation_sources_independent
        operation_sources_declared
        operation_sources_mismatch
        operation_sources_misattributed
        operation_sources_plugin
        operation_sources_role
        operation_sources_recovery_source
        operation_sources_undeclared
        operation_sources_ancestor
        operation_subtree_input_suspend
        operation_subtree_provider_guard
        operation_subtree_recovery_config
        operation_subtree_recovery_realm
        operation_subtree_recovery_requirements
        operation_subtree_recovery_outputs
        operation_subtree_recovery_equivalent
        operation_outputs
        operation_outputs_staged
        operation_outputs_held
        operation_outputs_retiring
        operation_graph
        operation_graph_cycle
        operation_graph_conflict
        operation_paths_conflict
        operation_paths_parallel
        operation_session_close_preflight
        operation_session_close_state_capture
        operation_session_close_state_restore
        operation_session_close_preflight_kernel
        operation_session_close_preflight_available
        operation_session_close_preflight_throw
        operation_session_close
        operation_session_close_prepared
        operation_session_close_stopping
        operation_session_close_commit
        operation_session_close_recovery_commit
        operation_session_close_recovery_failure
        operation
        operation_recovery
        operation_child_failure
        operation_cancel
        operation_cancel_commit
        operation_recovery_mapping
        operation_sessions
        operation_sessions_recovery
        operation_sessions_prepare_failure
        operation_sessions_prepare_cancel
        operation_guards
        operation_state_preserved
        operation_merge
        operation_owner
        operation_owner_before_create
        operation_owner_recovery_failure
        operation_owner_recovery_after_create_failure
        operation_owner_contract
        operation_owner_delivery
        operation_owner_kernel_close
        operation_owner_kernel_close_prepared
        operation_owner_kernel_close_stopping
        operation_subtree_provider_loss
        operation_subtree_future_realm
        operation_subtree_future_version
        operation_subtree_future_missing_service
        operation_subtree_future_cycle
        operation_subtree_future_child
        operation_subtree_future_contract
        operation_subtree_future_missing
        operation_subtree
        operation_subtree_omit
        operation_subtree_parent
        operation_subtree_nested
        operation_subtree_nested_omit
        operation_subtree_nested_stale
        operation_waiting
        operation_waiting_recovery
        operation_waiting_root
        operation_unstarted
        operation_unstarted_recovery
    )
        if(operation STREQUAL "operation_owner_delivery" AND backend STREQUAL "qt")
            continue() # Qt 业务入口要求实际 QtScheduler；跨线程拒投在共用宿主层用原生后端验证。
        endif()
        add_test(NAME p5.${backend}_${operation} COMMAND "${CMAKE_COMMAND}"
            "-DTEST_EXE=$<TARGET_FILE:eve_module_reservation_tests>" "-DCASE=${backend}_${operation}"
            "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/reservation-cases"
            "-DFIXTURES=$<TARGET_FILE:${old}>;$<TARGET_FILE:${next}>;$<TARGET_FILE:${third}>;$<TARGET_FILE:eve_reservation_peer_old>;$<TARGET_FILE:eve_reservation_peer_next>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/module_native_case.cmake")
        set_tests_properties(p5.${backend}_${operation} PROPERTIES LABELS "p5;module;${backend};replacement_operation" TIMEOUT 25)
        if(operation STREQUAL "operation_sources_raw_loss")
            set_property(TEST p5.${backend}_${operation} PROPERTY ENVIRONMENT
                "EVE_TEST_RAW_SOURCE_DLL=$<TARGET_FILE:eve_fixture_services>")
        endif()
    endforeach()
    add_test(NAME p5.${backend}_configuration_recovery COMMAND "${CMAKE_COMMAND}"
        "-DTEST_EXE=$<TARGET_FILE:eve_module_reservation_tests>" "-DCASE=${backend}_configuration_recovery"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/reservation-cases"
        "-DFIXTURES=$<TARGET_FILE:${old}>;$<TARGET_FILE:${next}>;$<TARGET_FILE:${third}>;$<TARGET_FILE:eve_reservation_peer_old>;$<TARGET_FILE:eve_reservation_peer_next>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/module_native_case.cmake")
    set_tests_properties(p5.${backend}_configuration_recovery PROPERTIES LABELS "p5;module;${backend};configuration_commit" TIMEOUT 25)
    add_test(NAME p5.${backend}_configuration_commit COMMAND "${CMAKE_COMMAND}"
        "-DTEST_EXE=$<TARGET_FILE:eve_module_reservation_tests>" "-DCASE=${backend}_configuration_commit"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/reservation-cases"
        "-DFIXTURES=$<TARGET_FILE:${old}>;$<TARGET_FILE:${next}>;$<TARGET_FILE:${third}>;$<TARGET_FILE:eve_reservation_peer_old>;$<TARGET_FILE:eve_reservation_peer_next>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/module_native_case.cmake")
    set_tests_properties(p5.${backend}_configuration_commit PROPERTIES LABELS "p5;module;${backend};configuration_commit" TIMEOUT 25)
    add_test(NAME p5.${backend}_runtime_origin COMMAND "${CMAKE_COMMAND}"
        "-DTEST_EXE=$<TARGET_FILE:eve_module_origin_tests>" "-DCASE=${backend}"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/origin-cases"
        "-DFIXTURES=$<TARGET_FILE:${old}>;$<TARGET_FILE:${next}>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/module_native_case.cmake")
    set_tests_properties(p5.${backend}_runtime_origin PROPERTIES LABELS "p5;module;${backend};runtime_origin" TIMEOUT 25)
    set(cases guards admission drain released worker candidate_live failure)
    if(backend STREQUAL "native")
        list(APPEND cases parallel race)
    else()
        list(APPEND cases query_reentry create_reentry stage_reentry sibling_query_reentry)
    endif()
    foreach(case IN LISTS cases)
        add_test(NAME p5.${backend}_reservation_${case} COMMAND "${CMAKE_COMMAND}"
            "-DTEST_EXE=$<TARGET_FILE:eve_module_reservation_tests>" "-DCASE=${backend}_${case}"
            "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/reservation-cases"
            "-DFIXTURES=$<TARGET_FILE:${old}>;$<TARGET_FILE:${next}>;$<TARGET_FILE:${third}>;$<TARGET_FILE:eve_reservation_peer_old>;$<TARGET_FILE:eve_reservation_peer_next>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/module_native_case.cmake")
        set_tests_properties(p5.${backend}_reservation_${case} PROPERTIES LABELS "p5;module;${backend};replacement_reservation" TIMEOUT 25)
    endforeach()
    add_test(NAME p5.${backend}_definition_binding COMMAND "${CMAKE_COMMAND}"
        "-DTEST_EXE=$<TARGET_FILE:eve_module_reservation_tests>" "-DCASE=${backend}_definitions"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/reservation-cases"
        "-DFIXTURES=$<TARGET_FILE:${old}>;$<TARGET_FILE:${next}>;$<TARGET_FILE:${third}>;$<TARGET_FILE:eve_reservation_peer_old>;$<TARGET_FILE:eve_reservation_peer_next>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/module_native_case.cmake")
    set_tests_properties(p5.${backend}_definition_binding PROPERTIES LABELS "p5;module;${backend};definitions" TIMEOUT 25)
endfunction()
eve_reservation_cases(native eve_fixture_business eve_reservation_native_next eve_candidate_native_next)
if(TARGET eve_module_qt)
    set(directory "${CMAKE_CURRENT_BINARY_DIR}/reservation-qt/$<CONFIG>")
    eve_qt_toolchain_json(toolchain)
    file(GENERATE OUTPUT "${directory}/qt_metadata.json"
        CONTENT "{\"moduleId\":\"org.eve.qt.example\",\"moduleVersion\":\"2.0.0\",\"plugins\":[\"org.eve.qt.page\",\"org.eve.qt.settings\"],\"toolchain\":${toolchain}}")
    set(includes "-I${PROJECT_SOURCE_DIR}/sdk/include" "-I${directory}")
    foreach(include IN LISTS Qt5Core_INCLUDE_DIRS)
        list(APPEND includes "-I${include}")
    endforeach()
    add_custom_command(OUTPUT "${directory}/qt_business.moc"
        COMMAND Qt5::moc ${includes} "${PROJECT_SOURCE_DIR}/tests/fixtures/qt_business.cpp" -o "${directory}/qt_business.moc"
        DEPENDS "${PROJECT_SOURCE_DIR}/tests/fixtures/qt_business.cpp" "${directory}/qt_metadata.json"
            "${PROJECT_SOURCE_DIR}/sdk/include/eve/module/qt_plugin.hpp" VERBATIM)
    add_library(eve_reservation_qt_next MODULE "${PROJECT_SOURCE_DIR}/tests/fixtures/qt_business.cpp" "${directory}/qt_business.moc")
    target_include_directories(eve_reservation_qt_next PRIVATE "${directory}")
    target_link_libraries(eve_reservation_qt_next PRIVATE Eve::QtPluginSdk)
    set_target_properties(eve_reservation_qt_next PROPERTIES PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/replacement-fixtures")
    eve_target_defaults(eve_reservation_qt_next)
    target_link_libraries(eve_module_reservation_tests PRIVATE Eve::ModuleQt Eve::Qt)
    target_link_libraries(eve_module_origin_tests PRIVATE Eve::ModuleQt Eve::Qt)
    target_compile_definitions(eve_module_origin_tests PRIVATE EVE_TEST_QT_ORIGIN)
    add_dependencies(eve_module_origin_tests eve_qt_business_fixture eve_reservation_qt_next)
    target_compile_definitions(eve_module_reservation_tests PRIVATE EVE_TEST_QT_RESERVATION)
    add_dependencies(eve_module_reservation_tests eve_qt_business_fixture eve_reservation_qt_next eve_candidate_qt_next)
    eve_reservation_cases(qt eve_qt_business_fixture eve_reservation_qt_next eve_candidate_qt_next)
    foreach(version IN ITEMS old next)
        add_library(eve_usage_${version} MODULE "${PROJECT_SOURCE_DIR}/tests/fixtures/module_fixture.c"
            "${PROJECT_SOURCE_DIR}/tests/fixtures/business_fixture.c")
        eve_abi_test_defaults(eve_usage_${version})
        target_compile_definitions(eve_usage_${version} PRIVATE EVE_MODULE_EXPORTS EVE_ABI_EXPORTS
            FIXTURE_ID="org.eve.usage" FIXTURE_VERSION="$<IF:$<STREQUAL:${version},old>,1.0.0,2.0.0>")
        set_target_properties(eve_usage_${version} PROPERTIES PREFIX ""
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/replacement-fixtures")
    endforeach()
    add_executable(eve_module_usage_tests qt/module_usage.cpp)
    target_link_libraries(eve_module_usage_tests PRIVATE Eve::ModuleHost Eve::ModuleQt Eve::Qt Qt5::Core)
    eve_target_defaults(eve_module_usage_tests)
    add_dependencies(eve_module_usage_tests eve_fixture_business eve_reservation_native_next
        eve_qt_business_fixture eve_reservation_qt_next eve_usage_old eve_usage_next)
    add_test(NAME p5.module_shared_usage COMMAND "${CMAKE_COMMAND}"
        "-DTEST_EXE=$<TARGET_FILE:eve_module_usage_tests>" "-DCASE=shared"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/usage-cases"
        "-DFIXTURES=$<TARGET_FILE:eve_fixture_business>;$<TARGET_FILE:eve_reservation_native_next>;$<TARGET_FILE:eve_qt_business_fixture>;$<TARGET_FILE:eve_reservation_qt_next>;$<TARGET_FILE:eve_usage_old>;$<TARGET_FILE:eve_usage_next>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/module_native_case.cmake")
    set_tests_properties(p5.module_shared_usage PROPERTIES LABELS "p5;module;native;qt;replacement_usage" TIMEOUT 25)
    add_test(NAME p5.module_shared_usage_failure COMMAND "${CMAKE_COMMAND}"
        "-DTEST_EXE=$<TARGET_FILE:eve_module_usage_tests>" "-DCASE=failure"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/usage-cases"
        "-DFIXTURES=$<TARGET_FILE:eve_fixture_business>;$<TARGET_FILE:eve_reservation_native_next>;$<TARGET_FILE:eve_qt_business_fixture>;$<TARGET_FILE:eve_reservation_qt_next>;$<TARGET_FILE:eve_usage_old>;$<TARGET_FILE:eve_usage_next>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/module_native_case.cmake")
    set_tests_properties(p5.module_shared_usage_failure PROPERTIES LABELS "p5;module;native;qt;replacement_usage" TIMEOUT 25)
endif()
