function(eve_load_policy_fixture name id version)
    add_library(eve_fixture_policy_${name} MODULE
        "${PROJECT_SOURCE_DIR}/tests/fixtures/module_fixture.c"
        "${PROJECT_SOURCE_DIR}/tests/fixtures/business_fixture.c")
    eve_abi_test_defaults(eve_fixture_policy_${name})
    target_compile_definitions(eve_fixture_policy_${name} PRIVATE
        EVE_MODULE_EXPORTS EVE_ABI_EXPORTS FIXTURE_EXPLICIT_UNLOAD
        FIXTURE_ID="${id}" FIXTURE_VERSION="${version}" ${ARGN})
    set_target_properties(eve_fixture_policy_${name} PROPERTIES PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/load-policy-fixtures"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/load-policy-fixtures")
endfunction()
eve_load_policy_fixture(explicit org.eve.policy 1.0.0)
eve_load_policy_fixture(next org.eve.policy 2.0.0)
eve_load_policy_fixture(blocking org.eve.policy.blocking 1.0.0 FIXTURE_BLOCKING)
eve_load_policy_fixture(flags org.eve.policy 1.0.0 FIXTURE_BAD_FLAGS)
eve_load_policy_fixture(minor org.eve.policy 1.0.0 FIXTURE_BAD_MINOR)

add_executable(eve_module_load_policy_tests "${PROJECT_SOURCE_DIR}/tests/qt/module_load_policy.cpp")
target_link_libraries(eve_module_load_policy_tests PRIVATE Eve::ModuleNative Qt5::Core)
eve_target_defaults(eve_module_load_policy_tests)
add_dependencies(eve_module_load_policy_tests eve_fixture_business eve_fixture_bad_flags
    eve_fixture_policy_explicit eve_fixture_policy_next eve_fixture_policy_blocking
    eve_fixture_policy_flags eve_fixture_policy_minor)
foreach(case IN ITEMS approved resident capable_resident wire unauthorized wrong_id wrong_hash invalid_entry
    tampered_preview legacy_reject legacy_flags explicit_flags explicit_minor reuse immutable_resident
    immutable_approved alias_resident alias_approved candidates same_hash_copy threads reentry)
    add_test(NAME p5.load_policy_${case} COMMAND "${CMAKE_COMMAND}"
        "-DTEST_EXE=$<TARGET_FILE:eve_module_load_policy_tests>" "-DCASE=${case}"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/load-policy-cases"
        "-DFIXTURES=$<TARGET_FILE:eve_fixture_business>;$<TARGET_FILE:eve_fixture_policy_explicit>;$<TARGET_FILE:eve_fixture_policy_next>;$<TARGET_FILE:eve_fixture_policy_blocking>;$<TARGET_FILE:eve_fixture_bad_flags>;$<TARGET_FILE:eve_fixture_policy_flags>;$<TARGET_FILE:eve_fixture_policy_minor>"
        -P "${PROJECT_SOURCE_DIR}/tests/module_native_case.cmake")
    set_tests_properties(p5.load_policy_${case} PROPERTIES LABELS "p5;module;native;load_policy" TIMEOUT 25)
endforeach()
