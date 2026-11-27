eve_unload_fixture(state_next org.eve.state business_fixture.c
    "${PROJECT_SOURCE_DIR}/tests/fixtures/business_throw.cpp")
target_compile_definitions(eve_fixture_unload_state_next PRIVATE
    FIXTURE_STATE FIXTURE_STATE_VERSION=3 FIXTURE_VERSION="2.0.0")

add_executable(eve_module_unload_replacement_tests
    "${PROJECT_SOURCE_DIR}/tests/qt/module_unload_replacement_main.cpp"
    "${PROJECT_SOURCE_DIR}/tests/qt/module_unload_replacement.cpp"
    "${PROJECT_SOURCE_DIR}/tests/qt/module_unload_configuration.cpp"
    "${PROJECT_SOURCE_DIR}/tests/qt/module_unload_concurrent.cpp")
target_link_libraries(eve_module_unload_replacement_tests PRIVATE
    Eve::Replacement Eve::ModuleHost Eve::Qt Eve::ConfigStoreQt Qt5::Core)
eve_target_defaults(eve_module_unload_replacement_tests)
add_dependencies(eve_module_unload_replacement_tests
    eve_fixture_unload_basic eve_fixture_unload_lifecycle eve_fixture_unload_services
    eve_fixture_unload_state eve_fixture_policy_flags eve_fixture_dependent
    eve_fixture_unload_next eve_fixture_unload_state_next)
foreach(case IN ITEMS replacement_applied replacement_release_retry replacement_restore_reject
    replacement_start_failure replacement_cancel_restore replacement_recovery_failure replacement_restore_pending
    configured configured_definitions configured_unload_failure parallel_owners)
    add_test(NAME p5.unload_${case} COMMAND "${CMAKE_COMMAND}"
        "-DTEST_EXE=$<TARGET_FILE:eve_module_unload_replacement_tests>" "-DCASE=${case}"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/unload-replacement-cases" "-DCASE_TIMEOUT=75"
        "-DFIXTURES=$<TARGET_FILE:eve_fixture_unload_basic>;$<TARGET_FILE:eve_fixture_unload_lifecycle>;$<TARGET_FILE:eve_fixture_unload_services>;$<TARGET_FILE:eve_fixture_unload_state>;$<TARGET_FILE:eve_fixture_policy_flags>;$<TARGET_FILE:eve_fixture_dependent>;$<TARGET_FILE:eve_fixture_unload_next>;$<TARGET_FILE:eve_fixture_unload_state_next>"
        -P "${PROJECT_SOURCE_DIR}/tests/module_native_case.cmake")
    set_tests_properties(p5.unload_${case} PROPERTIES LABELS "p5;module;native;unload;unload_replacement" TIMEOUT 90)
endforeach()
