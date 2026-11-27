set(qt_ui_directory "${CMAKE_CURRENT_BINARY_DIR}/qt-ui/$<CONFIG>")
set(qt_ui_metadata "${CMAKE_CURRENT_BINARY_DIR}/qt-preflight/good/$<CONFIG>")
set(qt_ui_moc_includes "-I${PROJECT_SOURCE_DIR}/sdk/include" "-I${qt_ui_metadata}")
foreach(directory IN LISTS Qt5Core_INCLUDE_DIRS)
    list(APPEND qt_ui_moc_includes "-I${directory}")
endforeach()
add_custom_command(OUTPUT "${qt_ui_directory}/qt_ui.moc"
    COMMAND Qt5::moc ${qt_ui_moc_includes} "${PROJECT_SOURCE_DIR}/tests/fixtures/qt_ui.cpp"
        -o "${qt_ui_directory}/qt_ui.moc"
    DEPENDS "${PROJECT_SOURCE_DIR}/tests/fixtures/qt_ui.cpp" "${qt_ui_metadata}/qt_metadata.json"
        "${PROJECT_SOURCE_DIR}/sdk/include/eve/module/qt_plugin.hpp" VERBATIM)
add_library(eve_qt_ui_fixture MODULE fixtures/qt_ui.cpp "${qt_ui_directory}/qt_ui.moc")
set_target_properties(eve_qt_ui_fixture PROPERTIES PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/qt-ui" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/qt-ui")
target_include_directories(eve_qt_ui_fixture PRIVATE "${qt_ui_directory}")
target_link_libraries(eve_qt_ui_fixture PRIVATE Eve::QtUiPluginSdk)
eve_target_defaults(eve_qt_ui_fixture)
add_executable(eve_module_qt_ui_tests qt/module_qt_ui.cpp)
target_link_libraries(eve_module_qt_ui_tests PRIVATE Eve::ModuleQt eve_desktop_impl)
eve_target_defaults(eve_module_qt_ui_tests)
add_dependencies(eve_module_qt_ui_tests eve_qt_ui_fixture)
foreach(case IN ITEMS scheduler lifetime multi update)
    add_test(NAME p4.qt_ui_${case} COMMAND "${CMAKE_COMMAND}"
        "-DTEST_EXE=$<TARGET_FILE:eve_module_qt_ui_tests>" "-DCASE=${case}"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/qt-ui-cases" "-DFIXTURES=$<TARGET_FILE:eve_qt_ui_fixture>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/module_native_case.cmake")
    set_tests_properties(p4.qt_ui_${case} PROPERTIES LABELS "p4;module;qt;ui" TIMEOUT 25
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()
