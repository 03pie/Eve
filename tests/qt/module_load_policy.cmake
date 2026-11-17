function(cordis_load_policy_fixture name id version)
    add_library(cordis_fixture_policy_${name} MODULE
        "${PROJECT_SOURCE_DIR}/tests/fixtures/module_fixture.c"
        "${PROJECT_SOURCE_DIR}/tests/fixtures/business_fixture.c")
    cordis_abi_test_defaults(cordis_fixture_policy_${name})
    target_compile_definitions(cordis_fixture_policy_${name} PRIVATE
        CORDIS_MODULE_EXPORTS CORDIS_ABI_EXPORTS FIXTURE_EXPLICIT_UNLOAD
        FIXTURE_ID="${id}" FIXTURE_VERSION="${version}" ${ARGN})
    set_target_properties(cordis_fixture_policy_${name} PROPERTIES PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/load-policy-fixtures"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/load-policy-fixtures")
endfunction()
cordis_load_policy_fixture(explicit org.cordis.policy 1.0.0)
cordis_load_policy_fixture(next org.cordis.policy 2.0.0)
cordis_load_policy_fixture(blocking org.cordis.policy.blocking 1.0.0 FIXTURE_BLOCKING)
cordis_load_policy_fixture(flags org.cordis.policy 1.0.0 FIXTURE_BAD_FLAGS)
cordis_load_policy_fixture(minor org.cordis.policy 1.0.0 FIXTURE_BAD_MINOR)

add_executable(cordis_module_load_policy_tests "${PROJECT_SOURCE_DIR}/tests/qt/module_load_policy.cpp")
target_link_libraries(cordis_module_load_policy_tests PRIVATE Cordis::ModuleNative Qt5::Core)
cordis_target_defaults(cordis_module_load_policy_tests)
add_dependencies(cordis_module_load_policy_tests cordis_fixture_business cordis_fixture_bad_flags
    cordis_fixture_policy_explicit cordis_fixture_policy_next cordis_fixture_policy_blocking
    cordis_fixture_policy_flags cordis_fixture_policy_minor)
foreach(case IN ITEMS approved resident capable_resident wire unauthorized wrong_id wrong_hash invalid_entry
    tampered_preview legacy_reject legacy_flags explicit_flags explicit_minor reuse immutable_resident
    immutable_approved alias_resident alias_approved candidates same_hash_copy threads reentry)
    add_test(NAME p5.load_policy_${case} COMMAND "${CMAKE_COMMAND}"
        "-DTEST_EXE=$<TARGET_FILE:cordis_module_load_policy_tests>" "-DCASE=${case}"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/load-policy-cases"
        "-DFIXTURES=$<TARGET_FILE:cordis_fixture_business>;$<TARGET_FILE:cordis_fixture_policy_explicit>;$<TARGET_FILE:cordis_fixture_policy_next>;$<TARGET_FILE:cordis_fixture_policy_blocking>;$<TARGET_FILE:cordis_fixture_bad_flags>;$<TARGET_FILE:cordis_fixture_policy_flags>;$<TARGET_FILE:cordis_fixture_policy_minor>"
        -P "${PROJECT_SOURCE_DIR}/tests/module_native_case.cmake")
    set_tests_properties(p5.load_policy_${case} PROPERTIES LABELS "p5;module;native;load_policy" TIMEOUT 25)
endforeach()
