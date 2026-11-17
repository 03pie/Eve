cordis_unload_fixture(state_next org.cordis.state business_fixture.c
    "${PROJECT_SOURCE_DIR}/tests/fixtures/business_throw.cpp")
target_compile_definitions(cordis_fixture_unload_state_next PRIVATE
    FIXTURE_STATE FIXTURE_STATE_VERSION=3 FIXTURE_VERSION="2.0.0")

add_executable(cordis_module_unload_replacement_tests
    "${PROJECT_SOURCE_DIR}/tests/qt/module_unload_replacement_main.cpp"
    "${PROJECT_SOURCE_DIR}/tests/qt/module_unload_replacement.cpp"
    "${PROJECT_SOURCE_DIR}/tests/qt/module_unload_configuration.cpp"
    "${PROJECT_SOURCE_DIR}/tests/qt/module_unload_concurrent.cpp")
target_link_libraries(cordis_module_unload_replacement_tests PRIVATE
    Cordis::Replacement Cordis::ModuleHost Cordis::Qt Cordis::ConfigStoreQt Qt5::Core)
cordis_target_defaults(cordis_module_unload_replacement_tests)
add_dependencies(cordis_module_unload_replacement_tests
    cordis_fixture_unload_basic cordis_fixture_unload_lifecycle cordis_fixture_unload_services
    cordis_fixture_unload_state cordis_fixture_policy_flags cordis_fixture_dependent
    cordis_fixture_unload_next cordis_fixture_unload_state_next)
foreach(case IN ITEMS replacement_applied replacement_release_retry replacement_restore_reject
    replacement_start_failure replacement_cancel_restore replacement_recovery_failure replacement_restore_pending
    configured configured_definitions configured_unload_failure parallel_owners)
    add_test(NAME p5.unload_${case} COMMAND "${CMAKE_COMMAND}"
        "-DTEST_EXE=$<TARGET_FILE:cordis_module_unload_replacement_tests>" "-DCASE=${case}"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/unload-replacement-cases" "-DCASE_TIMEOUT=75"
        "-DFIXTURES=$<TARGET_FILE:cordis_fixture_unload_basic>;$<TARGET_FILE:cordis_fixture_unload_lifecycle>;$<TARGET_FILE:cordis_fixture_unload_services>;$<TARGET_FILE:cordis_fixture_unload_state>;$<TARGET_FILE:cordis_fixture_policy_flags>;$<TARGET_FILE:cordis_fixture_dependent>;$<TARGET_FILE:cordis_fixture_unload_next>;$<TARGET_FILE:cordis_fixture_unload_state_next>"
        -P "${PROJECT_SOURCE_DIR}/tests/module_native_case.cmake")
    set_tests_properties(p5.unload_${case} PROPERTIES LABELS "p5;module;native;unload;unload_replacement" TIMEOUT 90)
endforeach()
