foreach(mode IN ITEMS next rejected)
    add_library(eve_candidate_native_${mode} MODULE "${PROJECT_SOURCE_DIR}/tests/fixtures/module_fixture.c")
    eve_abi_test_defaults(eve_candidate_native_${mode})
    target_compile_definitions(eve_candidate_native_${mode} PRIVATE EVE_MODULE_EXPORTS
        FIXTURE_ID="org.eve.business" FIXTURE_VERSION="2.0.0")
    if(mode STREQUAL "rejected")
        target_compile_definitions(eve_candidate_native_${mode} PRIVATE FIXTURE_MODE=1)
    endif()
    set_target_properties(eve_candidate_native_${mode} PROPERTIES PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/candidate-fixtures"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/candidate-fixtures")
endforeach()
add_executable(eve_module_candidates_tests qt/module_candidates.cpp)
target_link_libraries(eve_module_candidates_tests PRIVATE Eve::ModuleNative Eve::ModuleHost Qt5::Core)
eve_target_defaults(eve_module_candidates_tests)
add_dependencies(eve_module_candidates_tests eve_fixture_business eve_candidate_native_next eve_candidate_native_rejected)
function(eve_candidate_cases backend previous next rejected)
    set(cases coexist reuse guards catalog identity failed failed_current)
    if(backend STREQUAL "native")
        list(APPEND cases drained live budget physical aliases business_failed_current business_failed_candidate)
    else()
        list(APPEND cases reentry)
    endif()
    foreach(case IN LISTS cases)
        add_test(NAME p5.${backend}_candidate_${case} COMMAND "${CMAKE_COMMAND}"
            "-DTEST_EXE=$<TARGET_FILE:eve_module_candidates_tests>" "-DCASE=${backend}_${case}"
            "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/candidate-cases"
            "-DFIXTURES=$<TARGET_FILE:${previous}>;$<TARGET_FILE:${next}>;$<TARGET_FILE:${rejected}>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/module_native_case.cmake")
        set_tests_properties(p5.${backend}_candidate_${case} PROPERTIES LABELS "p5;module;${backend};candidates" TIMEOUT 25)
    endforeach()
endfunction()
eve_candidate_cases(native eve_fixture_business eve_candidate_native_next eve_candidate_native_rejected)
if(TARGET eve_module_qt)
    include("${PROJECT_SOURCE_DIR}/cmake/QtPluginContract.cmake")
    eve_qt_toolchain_json(toolchain)
    foreach(mode IN ITEMS next rejected)
        set(directory "${CMAKE_CURRENT_BINARY_DIR}/candidate-metadata/${mode}/$<CONFIG>")
        file(GENERATE OUTPUT "${directory}/qt_metadata.json"
            CONTENT "{\"moduleId\":\"org.eve.qt.example\",\"moduleVersion\":\"2.0.0\",\"plugins\":[\"org.eve.qt.page\",\"org.eve.qt.settings\"],\"toolchain\":${toolchain}}")
        add_custom_command(OUTPUT "${directory}/qt_metadata.moc"
            COMMAND Qt5::moc "-I${directory}" "${PROJECT_SOURCE_DIR}/tests/fixtures/qt_metadata.cpp"
                -o "${directory}/qt_metadata.moc"
            DEPENDS "${PROJECT_SOURCE_DIR}/tests/fixtures/qt_metadata.cpp" "${directory}/qt_metadata.json" VERBATIM)
        add_library(eve_candidate_qt_${mode} MODULE "${PROJECT_SOURCE_DIR}/tests/fixtures/qt_metadata.cpp"
            "${directory}/qt_metadata.moc")
        target_include_directories(eve_candidate_qt_${mode} PRIVATE "${directory}")
        target_link_libraries(eve_candidate_qt_${mode} PRIVATE Qt5::Core)
        set_target_properties(eve_candidate_qt_${mode} PROPERTIES PREFIX ""
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/candidate-fixtures"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/candidate-fixtures")
        if(mode STREQUAL "next")
            target_compile_definitions(eve_candidate_qt_${mode} PRIVATE EVE_QT_ROOT_REENTER)
        else()
            target_compile_definitions(eve_candidate_qt_${mode} PRIVATE EVE_QT_ROOT_PARENT)
        endif()
        eve_target_defaults(eve_candidate_qt_${mode})
    endforeach()
    target_link_libraries(eve_module_candidates_tests PRIVATE Eve::ModuleQt)
    target_compile_definitions(eve_module_candidates_tests PRIVATE EVE_TEST_QT_CANDIDATES)
    add_dependencies(eve_module_candidates_tests eve_qt_metadata_good eve_candidate_qt_next eve_candidate_qt_rejected)
    eve_candidate_cases(qt eve_qt_metadata_good eve_candidate_qt_next eve_candidate_qt_rejected)
endif()
