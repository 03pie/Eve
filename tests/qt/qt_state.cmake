set(qt_state_directory "${CMAKE_CURRENT_BINARY_DIR}/qt-state/$<CONFIG>")
add_custom_command(OUTPUT "${qt_state_directory}/qt_state.moc"
    COMMAND Qt5::moc ${qt_business_moc_includes} "${PROJECT_SOURCE_DIR}/tests/fixtures/qt_state.cpp"
        -o "${qt_state_directory}/qt_state.moc"
    DEPENDS "${PROJECT_SOURCE_DIR}/tests/fixtures/qt_state.cpp" "${qt_business_metadata}/qt_metadata.json"
        "${PROJECT_SOURCE_DIR}/sdk/include/eve/module/qt_plugin.hpp"
        "${PROJECT_SOURCE_DIR}/sdk/include/eve/module/qt_state_plugin.hpp" VERBATIM)
add_library(eve_qt_state_fixture MODULE "${PROJECT_SOURCE_DIR}/tests/fixtures/qt_state.cpp"
    "${qt_state_directory}/qt_state.moc")
set_target_properties(eve_qt_state_fixture PROPERTIES PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/qt-state"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/qt-state")
target_include_directories(eve_qt_state_fixture PRIVATE "${qt_state_directory}")
target_link_libraries(eve_qt_state_fixture PRIVATE Eve::QtPluginSdk)
eve_target_defaults(eve_qt_state_fixture)
add_executable(eve_module_state_qt_tests qt/module_state_qt.cpp)
target_link_libraries(eve_module_state_qt_tests PRIVATE Eve::ModuleQt Eve::Qt Qt5::Core)
eve_target_defaults(eve_module_state_qt_tests)
add_dependencies(eve_module_state_qt_tests eve_qt_state_fixture eve_qt_business_fixture)
foreach(case IN ITEMS metadata roundtrip origin_lifetime legacy stateless describe_bad describe_throw describe_pending
    schema_id schema_version validate_reject validate_pending validate_throw restore_reject restore_pending restore_throw
    missing_iid capture_json capture_pointer capture_pending capture_throw capture_parent capture_deleted
    capture_close restore_close capture_quit restore_quit object_guard reentry wrong_thread)
    add_test(NAME p5.qt_state_${case} COMMAND "${CMAKE_COMMAND}"
        "-DTEST_EXE=$<TARGET_FILE:eve_module_state_qt_tests>" "-DCASE=${case}"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/qt-state-cases"
        "-DFIXTURES=$<TARGET_FILE:eve_qt_state_fixture>;$<TARGET_FILE:eve_qt_business_fixture>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/module_native_case.cmake")
    set_tests_properties(p5.qt_state_${case} PROPERTIES LABELS "p5;module;qt;state" TIMEOUT 25)
endforeach()
include("${CMAKE_CURRENT_SOURCE_DIR}/qt/qt_state_operation.cmake")
