add_library(cordis_reservation_native_next MODULE "${PROJECT_SOURCE_DIR}/tests/fixtures/module_fixture.c"
    "${PROJECT_SOURCE_DIR}/tests/fixtures/business_fixture.c")
cordis_abi_test_defaults(cordis_reservation_native_next)
target_compile_definitions(cordis_reservation_native_next PRIVATE CORDIS_MODULE_EXPORTS CORDIS_ABI_EXPORTS
    FIXTURE_ID="org.cordis.business" FIXTURE_VERSION="2.0.0")
set_target_properties(cordis_reservation_native_next PROPERTIES PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/replacement-fixtures")
foreach(version IN ITEMS old next)
    add_library(cordis_reservation_peer_${version} MODULE "${PROJECT_SOURCE_DIR}/tests/fixtures/module_fixture.c")
    cordis_abi_test_defaults(cordis_reservation_peer_${version})
    target_compile_definitions(cordis_reservation_peer_${version} PRIVATE CORDIS_MODULE_EXPORTS
        FIXTURE_ID="org.cordis.peer" FIXTURE_VERSION="$<IF:$<STREQUAL:${version},old>,1.0.0,2.0.0>")
    set_target_properties(cordis_reservation_peer_${version} PROPERTIES PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/replacement-fixtures")
endforeach()
add_executable(cordis_module_reservation_tests qt/module_reservation.cpp)
target_link_libraries(cordis_module_reservation_tests PRIVATE Cordis::ModuleNative Cordis::ModuleHost Cordis::ConfigKernel Cordis::ConfigSession Cordis::ConfigStoreQt Qt5::Core)
cordis_target_defaults(cordis_module_reservation_tests)
add_dependencies(cordis_module_reservation_tests cordis_fixture_business cordis_reservation_native_next
    cordis_reservation_peer_old cordis_reservation_peer_next cordis_candidate_native_next)
function(cordis_reservation_cases backend old next third)
    set(cases guards admission drain released worker candidate_live failure)
    if(backend STREQUAL "native")
        list(APPEND cases parallel race)
    else()
        list(APPEND cases query_reentry create_reentry stage_reentry sibling_query_reentry)
    endif()
    foreach(case IN LISTS cases)
        add_test(NAME p5.${backend}_reservation_${case} COMMAND "${CMAKE_COMMAND}"
            "-DTEST_EXE=$<TARGET_FILE:cordis_module_reservation_tests>" "-DCASE=${backend}_${case}"
            "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/reservation-cases"
            "-DFIXTURES=$<TARGET_FILE:${old}>;$<TARGET_FILE:${next}>;$<TARGET_FILE:${third}>;$<TARGET_FILE:cordis_reservation_peer_old>;$<TARGET_FILE:cordis_reservation_peer_next>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/module_native_case.cmake")
        set_tests_properties(p5.${backend}_reservation_${case} PROPERTIES LABELS "p5;module;${backend};replacement_reservation" TIMEOUT 25)
    endforeach()
    add_test(NAME p5.${backend}_definition_binding COMMAND "${CMAKE_COMMAND}"
        "-DTEST_EXE=$<TARGET_FILE:cordis_module_reservation_tests>" "-DCASE=${backend}_definitions"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/reservation-cases"
        "-DFIXTURES=$<TARGET_FILE:${old}>;$<TARGET_FILE:${next}>;$<TARGET_FILE:${third}>;$<TARGET_FILE:cordis_reservation_peer_old>;$<TARGET_FILE:cordis_reservation_peer_next>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/module_native_case.cmake")
    set_tests_properties(p5.${backend}_definition_binding PROPERTIES LABELS "p5;module;${backend};definitions" TIMEOUT 25)
endfunction()
cordis_reservation_cases(native cordis_fixture_business cordis_reservation_native_next cordis_candidate_native_next)
if(TARGET cordis_module_qt)
    set(directory "${CMAKE_CURRENT_BINARY_DIR}/reservation-qt/$<CONFIG>")
    cordis_qt_toolchain_json(toolchain)
    file(GENERATE OUTPUT "${directory}/qt_metadata.json"
        CONTENT "{\"moduleId\":\"org.cordis.qt.example\",\"moduleVersion\":\"2.0.0\",\"plugins\":[\"org.cordis.qt.page\",\"org.cordis.qt.settings\"],\"toolchain\":${toolchain}}")
    set(includes "-I${PROJECT_SOURCE_DIR}/sdk/include" "-I${directory}")
    foreach(include IN LISTS Qt5Core_INCLUDE_DIRS)
        list(APPEND includes "-I${include}")
    endforeach()
    add_custom_command(OUTPUT "${directory}/qt_business.moc"
        COMMAND Qt5::moc ${includes} "${PROJECT_SOURCE_DIR}/tests/fixtures/qt_business.cpp" -o "${directory}/qt_business.moc"
        DEPENDS "${PROJECT_SOURCE_DIR}/tests/fixtures/qt_business.cpp" "${directory}/qt_metadata.json"
            "${PROJECT_SOURCE_DIR}/sdk/include/cordis/module/qt_plugin.hpp" VERBATIM)
    add_library(cordis_reservation_qt_next MODULE "${PROJECT_SOURCE_DIR}/tests/fixtures/qt_business.cpp" "${directory}/qt_business.moc")
    target_include_directories(cordis_reservation_qt_next PRIVATE "${directory}")
    target_link_libraries(cordis_reservation_qt_next PRIVATE Cordis::QtPluginSdk)
    set_target_properties(cordis_reservation_qt_next PROPERTIES PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/replacement-fixtures")
    cordis_target_defaults(cordis_reservation_qt_next)
    target_link_libraries(cordis_module_reservation_tests PRIVATE Cordis::ModuleQt Cordis::Qt)
    target_compile_definitions(cordis_module_reservation_tests PRIVATE CORDIS_TEST_QT_RESERVATION)
    add_dependencies(cordis_module_reservation_tests cordis_qt_business_fixture cordis_reservation_qt_next cordis_candidate_qt_next)
    cordis_reservation_cases(qt cordis_qt_business_fixture cordis_reservation_qt_next cordis_candidate_qt_next)
endif()
