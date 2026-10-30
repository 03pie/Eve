foreach(mode IN ITEMS next rejected)
    add_library(cordis_candidate_native_${mode} MODULE "${PROJECT_SOURCE_DIR}/tests/fixtures/module_fixture.c")
    cordis_abi_test_defaults(cordis_candidate_native_${mode})
    target_compile_definitions(cordis_candidate_native_${mode} PRIVATE CORDIS_MODULE_EXPORTS
        FIXTURE_ID="org.cordis.business" FIXTURE_VERSION="2.0.0")
    if(mode STREQUAL "rejected")
        target_compile_definitions(cordis_candidate_native_${mode} PRIVATE FIXTURE_MODE=1)
    endif()
    set_target_properties(cordis_candidate_native_${mode} PROPERTIES PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/candidate-fixtures"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/candidate-fixtures")
endforeach()
add_executable(cordis_module_candidates_tests qt/module_candidates.cpp)
target_link_libraries(cordis_module_candidates_tests PRIVATE Cordis::ModuleNative Cordis::ModuleHost Qt5::Core)
cordis_target_defaults(cordis_module_candidates_tests)
add_dependencies(cordis_module_candidates_tests cordis_fixture_business cordis_candidate_native_next cordis_candidate_native_rejected)
function(cordis_candidate_cases backend previous next rejected)
    set(cases coexist reuse guards catalog identity failed failed_current)
    if(backend STREQUAL "native")
        list(APPEND cases drained live budget physical aliases business_failed_current business_failed_candidate)
    else()
        list(APPEND cases reentry)
    endif()
    foreach(case IN LISTS cases)
        add_test(NAME p5.${backend}_candidate_${case} COMMAND "${CMAKE_COMMAND}"
            "-DTEST_EXE=$<TARGET_FILE:cordis_module_candidates_tests>" "-DCASE=${backend}_${case}"
            "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/candidate-cases"
            "-DFIXTURES=$<TARGET_FILE:${previous}>;$<TARGET_FILE:${next}>;$<TARGET_FILE:${rejected}>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/module_native_case.cmake")
        set_tests_properties(p5.${backend}_candidate_${case} PROPERTIES LABELS "p5;module;${backend};candidates" TIMEOUT 25)
    endforeach()
endfunction()
cordis_candidate_cases(native cordis_fixture_business cordis_candidate_native_next cordis_candidate_native_rejected)
if(TARGET cordis_module_qt)
    include("${PROJECT_SOURCE_DIR}/cmake/QtPluginContract.cmake")
    cordis_qt_toolchain_json(toolchain)
    foreach(mode IN ITEMS next rejected)
        set(directory "${CMAKE_CURRENT_BINARY_DIR}/candidate-metadata/${mode}/$<CONFIG>")
        file(GENERATE OUTPUT "${directory}/qt_metadata.json"
            CONTENT "{\"moduleId\":\"org.cordis.qt.example\",\"moduleVersion\":\"2.0.0\",\"plugins\":[\"org.cordis.qt.page\",\"org.cordis.qt.settings\"],\"toolchain\":${toolchain}}")
        add_custom_command(OUTPUT "${directory}/qt_metadata.moc"
            COMMAND Qt5::moc "-I${directory}" "${PROJECT_SOURCE_DIR}/tests/fixtures/qt_metadata.cpp"
                -o "${directory}/qt_metadata.moc"
            DEPENDS "${PROJECT_SOURCE_DIR}/tests/fixtures/qt_metadata.cpp" "${directory}/qt_metadata.json" VERBATIM)
        add_library(cordis_candidate_qt_${mode} MODULE "${PROJECT_SOURCE_DIR}/tests/fixtures/qt_metadata.cpp"
            "${directory}/qt_metadata.moc")
        target_include_directories(cordis_candidate_qt_${mode} PRIVATE "${directory}")
        target_link_libraries(cordis_candidate_qt_${mode} PRIVATE Qt5::Core)
        set_target_properties(cordis_candidate_qt_${mode} PROPERTIES PREFIX ""
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/candidate-fixtures"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/candidate-fixtures")
        if(mode STREQUAL "next")
            target_compile_definitions(cordis_candidate_qt_${mode} PRIVATE CORDIS_QT_ROOT_REENTER)
        else()
            target_compile_definitions(cordis_candidate_qt_${mode} PRIVATE CORDIS_QT_ROOT_PARENT)
        endif()
        cordis_target_defaults(cordis_candidate_qt_${mode})
    endforeach()
    target_link_libraries(cordis_module_candidates_tests PRIVATE Cordis::ModuleQt)
    target_compile_definitions(cordis_module_candidates_tests PRIVATE CORDIS_TEST_QT_CANDIDATES)
    add_dependencies(cordis_module_candidates_tests cordis_qt_metadata_good cordis_candidate_qt_next cordis_candidate_qt_rejected)
    cordis_candidate_cases(qt cordis_qt_metadata_good cordis_candidate_qt_next cordis_candidate_qt_rejected)
endif()
