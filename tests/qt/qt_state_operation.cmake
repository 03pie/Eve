set(qt_state_next_directory "${CMAKE_CURRENT_BINARY_DIR}/qt-state-next/$<CONFIG>")
eve_qt_toolchain_json(qt_state_next_toolchain)
file(GENERATE OUTPUT "${qt_state_next_directory}/qt_metadata.json"
    CONTENT "{\"moduleId\":\"org.eve.qt.example\",\"moduleVersion\":\"2.0.0\",\"plugins\":[\"org.eve.qt.page\",\"org.eve.qt.settings\"],\"toolchain\":${qt_state_next_toolchain}}")
add_custom_command(OUTPUT "${qt_state_next_directory}/qt_state.moc"
    COMMAND Qt5::moc "-I${qt_state_next_directory}" ${qt_business_moc_includes}
        "-DFIXTURE_STATE_VERSION=3" "${PROJECT_SOURCE_DIR}/tests/fixtures/qt_state.cpp"
        -o "${qt_state_next_directory}/qt_state.moc"
    DEPENDS "${PROJECT_SOURCE_DIR}/tests/fixtures/qt_state.cpp" "${qt_state_next_directory}/qt_metadata.json"
        "${PROJECT_SOURCE_DIR}/sdk/include/eve/module/qt_plugin.hpp"
        "${PROJECT_SOURCE_DIR}/sdk/include/eve/module/qt_state_plugin.hpp" VERBATIM)
add_library(eve_qt_state_next_fixture MODULE "${PROJECT_SOURCE_DIR}/tests/fixtures/qt_state.cpp"
    "${qt_state_next_directory}/qt_state.moc")
set_target_properties(eve_qt_state_next_fixture PROPERTIES PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/qt-state-next"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/qt-state-next")
target_include_directories(eve_qt_state_next_fixture PRIVATE "${qt_state_next_directory}")
target_compile_definitions(eve_qt_state_next_fixture PRIVATE FIXTURE_STATE_VERSION=3)
target_link_libraries(eve_qt_state_next_fixture PRIVATE Eve::QtPluginSdk)
eve_target_defaults(eve_qt_state_next_fixture)
add_executable(eve_module_state_operation_qt_tests qt/module_state_operation_qt.cpp)
target_link_libraries(eve_module_state_operation_qt_tests PRIVATE Eve::Replacement Eve::ModuleQt Eve::Qt Qt5::Core)
eve_target_defaults(eve_module_state_operation_qt_tests)
add_dependencies(eve_module_state_operation_qt_tests eve_qt_state_fixture eve_qt_state_next_fixture)
foreach(case IN ITEMS roundtrip capture_busy validate_reject restore_reject activate_fail recovery_restore recovery_validate
    recovery_activate cancel_capture cancel_validate cancel_restore cancel_activate close_capture close_restore
    capture_protocol validate_protocol restore_protocol capture_throw validate_throw restore_throw)
    add_test(NAME p5.state_operation_qt_${case} COMMAND "${CMAKE_COMMAND}"
        "-DTEST_EXE=$<TARGET_FILE:eve_module_state_operation_qt_tests>" "-DCASE=${case}"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/qt-state-operation-cases"
        "-DFIXTURES=$<TARGET_FILE:eve_qt_state_fixture>;$<TARGET_FILE:eve_qt_state_next_fixture>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/module_native_case.cmake")
    set_tests_properties(p5.state_operation_qt_${case} PROPERTIES LABELS "p5;module;qt;state;replacement_operation" TIMEOUT 25)
endforeach()
