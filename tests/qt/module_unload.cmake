function(eve_unload_fixture name id body)
    add_library(eve_fixture_unload_${name} MODULE
        "${PROJECT_SOURCE_DIR}/tests/fixtures/module_fixture.c"
        "${PROJECT_SOURCE_DIR}/tests/fixtures/module_unload_witness.c"
        "${PROJECT_SOURCE_DIR}/tests/fixtures/${body}" ${ARGN})
    eve_abi_test_defaults(eve_fixture_unload_${name})
    target_compile_definitions(eve_fixture_unload_${name} PRIVATE EVE_MODULE_EXPORTS EVE_ABI_EXPORTS
        FIXTURE_EXPLICIT_UNLOAD FIXTURE_ID="${id}")
    set_target_properties(eve_fixture_unload_${name} PROPERTIES PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/unload-fixtures"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/unload-fixtures")
endfunction()
eve_unload_fixture(basic org.eve.business business_fixture.c)
eve_unload_fixture(next org.eve.business business_fixture.c)
target_compile_definitions(eve_fixture_unload_next PRIVATE FIXTURE_VERSION="2.0.0")
eve_unload_fixture(lifecycle org.eve.lifecycle lifecycle_fixture.cpp)
eve_unload_fixture(services org.eve.services resource_fixture.c)
target_compile_definitions(eve_fixture_unload_services PRIVATE FIXTURE_SERVICES)
eve_unload_fixture(state org.eve.state business_fixture.c "${PROJECT_SOURCE_DIR}/tests/fixtures/business_throw.cpp")
target_compile_definitions(eve_fixture_unload_state PRIVATE FIXTURE_STATE)
add_executable(eve_module_unload_tests "${PROJECT_SOURCE_DIR}/tests/qt/module_unload.cpp"
    "${PROJECT_SOURCE_DIR}/tests/qt/module_unload_lifetime.cpp"
    "${PROJECT_SOURCE_DIR}/tests/qt/module_unload_reentry.cpp"
    "${PROJECT_SOURCE_DIR}/tests/qt/module_unload_lineage.cpp"
    "${PROJECT_SOURCE_DIR}/tests/qt/module_unload_reader.cpp")
target_link_libraries(eve_module_unload_tests PRIVATE Eve::ModuleHost Eve::Qt Qt5::Core)
eve_target_defaults(eve_module_unload_tests)
add_dependencies(eve_module_unload_tests eve_fixture_unload_basic eve_fixture_unload_lifecycle
    eve_fixture_unload_services eve_fixture_unload_state eve_fixture_policy_flags eve_fixture_dependent
    eve_fixture_unload_next)
foreach(case IN ITEMS physical file reload resident rejected release_retry external query_failure probe_other
    probe_release_failure reentry seal owner owner_gone last_lease heavy history session instance setup cleanup runtime state
    call worker qobject capture stale_capture cleanup_failure service closed_facade create_reentry
    lineage_candidate lineage_current lineage_reservation lineage_sibling reader)
    add_test(NAME p5.unload_${case} COMMAND "${CMAKE_COMMAND}"
        "-DTEST_EXE=$<TARGET_FILE:eve_module_unload_tests>" "-DCASE=${case}"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/unload-cases"
        "-DCASE_TIMEOUT=75"
        "-DFIXTURES=$<TARGET_FILE:eve_fixture_unload_basic>;$<TARGET_FILE:eve_fixture_unload_lifecycle>;$<TARGET_FILE:eve_fixture_unload_services>;$<TARGET_FILE:eve_fixture_unload_state>;$<TARGET_FILE:eve_fixture_policy_flags>;$<TARGET_FILE:eve_fixture_dependent>;$<TARGET_FILE:eve_fixture_unload_next>"
        -P "${PROJECT_SOURCE_DIR}/tests/module_native_case.cmake")
    set_tests_properties(p5.unload_${case} PROPERTIES LABELS "p5;module;native;unload" TIMEOUT 90)
endforeach()
