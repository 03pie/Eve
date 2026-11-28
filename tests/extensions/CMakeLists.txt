include("${PROJECT_SOURCE_DIR}/cmake/QtPluginContract.cmake")
eve_qt_toolchain_json(alias_toolchain)
set(alias_metadata_dir "${CMAKE_CURRENT_BINARY_DIR}/alias/$<CONFIG>")
file(GENERATE OUTPUT "${alias_metadata_dir}/logging_metadata.json"
    CONTENT "{\"moduleId\":\"extension\",\"moduleVersion\":\"1.0.0\",\"plugins\":[\"test.logging\"],\"toolchain\":${alias_toolchain}}")
add_library(eve_dev_alias_fixture MODULE ../logging/logging_fixture.cpp)
target_include_directories(eve_dev_alias_fixture PRIVATE "${alias_metadata_dir}")
target_link_libraries(eve_dev_alias_fixture PRIVATE Eve::QtUiPluginSdk)
set_target_properties(eve_dev_alias_fixture PROPERTIES AUTOMOC ON PREFIX "")
eve_target_defaults(eve_dev_alias_fixture)

add_executable(eve_business_extension_tests business_host.cpp)
target_link_libraries(eve_business_extension_tests PRIVATE eve_external_workbench Qt5::Test)
eve_target_defaults(eve_business_extension_tests)
add_dependencies(eve_business_extension_tests eve_logging_fixture eve_dev_alias_fixture eve_example_notebook eve_example_notebook_a)
add_test(NAME extensions.workspace_lifecycle COMMAND eve_business_extension_tests
    "${CMAKE_COMMAND}" "${PROJECT_SOURCE_DIR}/cmake/WriteExtensionPackage.cmake"
    "$<TARGET_FILE:eve_logging_fixture>" "$<TARGET_FILE:eve_example_notebook>" "$<TARGET_FILE:eve_example_notebook_a>"
    "$<TARGET_FILE:eve_dev_alias_fixture>")
set_tests_properties(extensions.workspace_lifecycle PROPERTIES LABELS "extensions;logging;qt;workbench"
    TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
