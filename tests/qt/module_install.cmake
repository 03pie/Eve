add_executable(cordis_module_install_tests qt/module_install.cpp)
target_link_libraries(cordis_module_install_tests PRIVATE Cordis::ModuleInstall Cordis::ModuleHost Qt5::Core)
cordis_target_defaults(cordis_module_install_tests)
add_dependencies(cordis_module_install_tests cordis_fixture_business cordis_candidate_native_next)
set(install_cases basic reuse manifest_name multichunk roots limits invalid_source source_busy lock occupied conflict
    concurrent long_path normalized_root unicode store_alias identity rollback rollback_foreign rollback_replaced publish_conflict redirected hardlink tamper native_stage)
set(install_fixtures "$<TARGET_FILE:cordis_fixture_business>;$<TARGET_FILE:cordis_candidate_native_next>;unused;unused")
if(TARGET cordis_module_qt)
    target_link_libraries(cordis_module_install_tests PRIVATE Cordis::ModuleQt)
    target_compile_definitions(cordis_module_install_tests PRIVATE CORDIS_TEST_QT_INSTALL)
    add_dependencies(cordis_module_install_tests cordis_qt_metadata_good cordis_candidate_qt_next)
    list(APPEND install_cases qt_stage)
    set(install_fixtures "$<TARGET_FILE:cordis_fixture_business>;$<TARGET_FILE:cordis_candidate_native_next>;$<TARGET_FILE:cordis_qt_metadata_good>;$<TARGET_FILE:cordis_candidate_qt_next>")
endif()
foreach(case IN LISTS install_cases)
    add_test(NAME p5.install_${case} COMMAND "${CMAKE_COMMAND}"
        "-DTEST_EXE=$<TARGET_FILE:cordis_module_install_tests>" "-DCASE=${case}"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/install-cases" "-DFIXTURES=${install_fixtures}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/module_native_case.cmake")
    set_tests_properties(p5.install_${case} PROPERTIES LABELS "p5;module;install" TIMEOUT 25)
endforeach()
