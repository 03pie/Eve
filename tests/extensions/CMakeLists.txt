add_executable(eve_business_extension_tests business_host.cpp)
target_link_libraries(eve_business_extension_tests PRIVATE eve_external_workbench Qt5::Test)
target_include_directories(eve_business_extension_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/ui")
eve_target_defaults(eve_business_extension_tests)
add_dependencies(eve_business_extension_tests eve_logging_fixture eve_example_notebook eve_example_notebook_a)
add_test(NAME extensions.workspace_lifecycle COMMAND eve_business_extension_tests
    "${CMAKE_COMMAND}" "${PROJECT_SOURCE_DIR}/cmake/WriteExtensionPackage.cmake"
    "$<TARGET_FILE:eve_logging_fixture>" "$<TARGET_FILE:eve_example_notebook>" "$<TARGET_FILE:eve_example_notebook_a>")
set_tests_properties(extensions.workspace_lifecycle PROPERTIES LABELS "extensions;logging;qt;workbench"
    TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
