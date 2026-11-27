add_library(eve_fixture_state_next MODULE
    "${PROJECT_SOURCE_DIR}/tests/fixtures/module_fixture.c"
    "${PROJECT_SOURCE_DIR}/tests/fixtures/business_fixture.c"
    "${PROJECT_SOURCE_DIR}/tests/fixtures/business_throw.cpp")
eve_abi_test_defaults(eve_fixture_state_next)
target_compile_definitions(eve_fixture_state_next PRIVATE EVE_MODULE_EXPORTS EVE_ABI_EXPORTS
    FIXTURE_ID="org.eve.state" FIXTURE_VERSION="2.0.0" FIXTURE_STATE FIXTURE_STATE_VERSION=3)
set_target_properties(eve_fixture_state_next PROPERTIES PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/state-operation-fixtures"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/state-operation-fixtures")

add_executable(eve_module_state_operation_native_tests
    "${PROJECT_SOURCE_DIR}/tests/qt/module_state_operation_native.cpp")
target_link_libraries(eve_module_state_operation_native_tests PRIVATE
    Eve::Replacement Eve::ModuleHost Eve::Qt Qt5::Core)
eve_target_defaults(eve_module_state_operation_native_tests)
add_dependencies(eve_module_state_operation_native_tests eve_fixture_state eve_fixture_state_next)
foreach(case IN ITEMS roundtrip busy validate_reject old_validate_reject late_validate_reject restore_reject
    start_failure partial_start recovery_restore recovery_start recovery_validate
    validate_pending restore_pending capture_pending
    cancel_capture cancel_validate cancel_restore cancel_start kernel_capture scheduler_capture)
    add_test(NAME p5.state_operation_native_${case} COMMAND "${CMAKE_COMMAND}"
        "-DTEST_EXE=$<TARGET_FILE:eve_module_state_operation_native_tests>" "-DCASE=${case}"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/native-state-operations"
        "-DFIXTURES=$<TARGET_FILE:eve_fixture_state>;$<TARGET_FILE:eve_fixture_state_next>"
        -P "${PROJECT_SOURCE_DIR}/tests/module_native_case.cmake")
    set_tests_properties(p5.state_operation_native_${case} PROPERTIES LABELS "p5;module;native;state_operation" TIMEOUT 25)
endforeach()
