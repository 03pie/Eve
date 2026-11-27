# Use the same embedded contract as the metadata fixture, with a real factory implementation.
set(qt_business_directory "${CMAKE_CURRENT_BINARY_DIR}/qt-business/$<CONFIG>")
set(qt_business_metadata "${CMAKE_CURRENT_BINARY_DIR}/qt-preflight/good/$<CONFIG>")
set(qt_business_moc_includes "-I${PROJECT_SOURCE_DIR}/sdk/include" "-I${qt_business_metadata}")
foreach(directory IN LISTS Qt5Core_INCLUDE_DIRS)
    list(APPEND qt_business_moc_includes "-I${directory}")
endforeach()
add_custom_command(OUTPUT "${qt_business_directory}/qt_business.moc"
    COMMAND Qt5::moc ${qt_business_moc_includes} "${PROJECT_SOURCE_DIR}/tests/fixtures/qt_business.cpp"
        -o "${qt_business_directory}/qt_business.moc"
    DEPENDS "${PROJECT_SOURCE_DIR}/tests/fixtures/qt_business.cpp" "${qt_business_metadata}/qt_metadata.json"
        "${PROJECT_SOURCE_DIR}/sdk/include/eve/module/qt_plugin.hpp" VERBATIM)
add_library(eve_qt_business_fixture MODULE "${PROJECT_SOURCE_DIR}/tests/fixtures/qt_business.cpp"
    "${qt_business_directory}/qt_business.moc")
set_target_properties(eve_qt_business_fixture PROPERTIES PREFIX "" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/qt-business" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/qt-business")
target_include_directories(eve_qt_business_fixture PRIVATE "${qt_business_directory}")
target_link_libraries(eve_qt_business_fixture PRIVATE Eve::QtPluginSdk)
eve_target_defaults(eve_qt_business_fixture)
add_executable(eve_module_qt_runtime_tests qt/module_qt_runtime.cpp)
target_link_libraries(eve_module_qt_runtime_tests PRIVATE Eve::ModuleQt Eve::Qt Qt5::Core)
eve_target_defaults(eve_module_qt_runtime_tests)
add_dependencies(eve_module_qt_runtime_tests eve_qt_business_fixture eve_qt_metadata_good)
foreach(case IN ITEMS normal multi update start_pending cleanup_pending start_failure activation_throw cleanup_failure
    null create_throw root parent duplicate wrong_instance bad_catalog query_throw missing_factory guards scheduler shared_core inject output tail_pending tail_failure wrong_core)
    set(fixture eve_qt_business_fixture)
    if(case STREQUAL "missing_factory")
        set(fixture eve_qt_metadata_good)
    endif()
    add_test(NAME p4.qt_runtime_${case} COMMAND "${CMAKE_COMMAND}"
        "-DTEST_EXE=$<TARGET_FILE:eve_module_qt_runtime_tests>" "-DCASE=${case}"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/qt-business-cases" "-DFIXTURES=$<TARGET_FILE:${fixture}>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/module_native_case.cmake")
    set_tests_properties(p4.qt_runtime_${case} PROPERTIES LABELS "p4;module;qt;runtime" TIMEOUT 25)
endforeach()

add_executable(eve_module_qt_reentry_tests qt/module_qt_reentry.cpp)
target_link_libraries(eve_module_qt_reentry_tests PRIVATE Eve::ModuleQt Eve::Qt Qt5::Core)
eve_target_defaults(eve_module_qt_reentry_tests)
add_dependencies(eve_module_qt_reentry_tests eve_qt_business_fixture)
foreach(case IN ITEMS create_stop create_update create_busy query)
    add_test(NAME p4.qt_reentry_${case} COMMAND "${CMAKE_COMMAND}"
        "-DTEST_EXE=$<TARGET_FILE:eve_module_qt_reentry_tests>" "-DCASE=${case}"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/qt-business-cases" "-DFIXTURES=$<TARGET_FILE:eve_qt_business_fixture>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/module_native_case.cmake")
    set_tests_properties(p4.qt_reentry_${case} PROPERTIES LABELS "p4;module;qt;runtime;reentry" TIMEOUT 25)
endforeach()
