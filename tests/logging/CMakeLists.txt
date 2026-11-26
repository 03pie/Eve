include("${PROJECT_SOURCE_DIR}/cmake/QtPluginContract.cmake")
cordis_qt_toolchain_json(logging_toolchain)
set(logging_generated "${CMAKE_CURRENT_BINARY_DIR}/metadata/$<CONFIG>")
file(GENERATE OUTPUT "${logging_generated}/logging_metadata.json"
    CONTENT "{\"moduleId\":\"org.cordis.test.logging\",\"moduleVersion\":\"1.0.0\",\"plugins\":[\"test.logging\"],\"toolchain\":${logging_toolchain}}")
add_library(cordis_logging_fixture MODULE logging_fixture.cpp)
target_include_directories(cordis_logging_fixture PRIVATE "${logging_generated}")
target_link_libraries(cordis_logging_fixture PRIVATE Cordis::QtUiPluginSdk)
set_target_properties(cordis_logging_fixture PROPERTIES AUTOMOC ON PREFIX "")
cordis_target_defaults(cordis_logging_fixture)

add_executable(cordis_logging_acceptance runtime_logging.cpp)
target_include_directories(cordis_logging_acceptance PRIVATE "${PROJECT_SOURCE_DIR}/src/ui")
target_link_libraries(cordis_logging_acceptance PRIVATE cordis_external_workbench Qt5::Test)
cordis_target_defaults(cordis_logging_acceptance)
add_dependencies(cordis_logging_acceptance cordis_plugin_workbench cordis_logging_fixture)

add_test(NAME logging.l1_cross_process COMMAND cordis_logging_acceptance "$<TARGET_FILE:cordis_logging_fixture>"
    "$<TARGET_FILE:cordis_plugin_workbench>" "${CMAKE_CURRENT_BINARY_DIR}/artifacts")
set_tests_properties(logging.l1_cross_process PROPERTIES LABELS "logging;qt;ui" TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
