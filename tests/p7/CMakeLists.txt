foreach(input IN ITEMS P7_READER_V1_MANIFEST P7_READER_V2_MANIFEST P7_TEMPLATE_MANIFEST)
    set(${input} "" CACHE FILEPATH "Actual external plugin manifest for P7 acceptance")
    if(NOT IS_ABSOLUTE "${${input}}")
        message(FATAL_ERROR "${input} must be an absolute path to an independently built module.json")
    endif()
endforeach()

add_executable(cordis_p7_acceptance integration.cpp)
target_link_libraries(cordis_p7_acceptance PRIVATE cordis_external_workbench Qt5::Test Ela::WidgetTools)
cordis_target_defaults(cordis_p7_acceptance)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/artifacts")
foreach(case IN ITEMS waiting multi diagnostics persistence upgrade close_busy feedback)
    add_test(NAME p7.external_${case} COMMAND cordis_p7_acceptance "${case}"
        "${P7_READER_V1_MANIFEST}" "${P7_READER_V2_MANIFEST}" "${CMAKE_CURRENT_BINARY_DIR}/artifacts")
    set_tests_properties(p7.external_${case} PROPERTIES LABELS "p7;module;qt;ui;external" TIMEOUT 180
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()
add_test(NAME p7.external_template COMMAND cordis_p7_acceptance template
    "${P7_TEMPLATE_MANIFEST}" "${P7_TEMPLATE_MANIFEST}" "${CMAKE_CURRENT_BINARY_DIR}/artifacts")
set_tests_properties(p7.external_template PROPERTIES LABELS "p7;module;qt;ui;external" TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

include("${PROJECT_SOURCE_DIR}/cmake/QtPluginContract.cmake")
cordis_qt_toolchain_json(u7_toolchain)
set(u7_generated "${CMAKE_CURRENT_BINARY_DIR}/u7-metadata/$<CONFIG>")
file(GENERATE OUTPUT "${u7_generated}/u7_metadata.json"
    CONTENT "{\"moduleId\":\"org.cordis.test.logs\",\"moduleVersion\":\"1.0.0\",\"plugins\":[\"u7.alpha\",\"u7.bravo\"],\"toolchain\":${u7_toolchain}}")
add_library(cordis_p7_log_fixture MODULE log_fixture.cpp)
target_include_directories(cordis_p7_log_fixture PRIVATE "${u7_generated}")
target_link_libraries(cordis_p7_log_fixture PRIVATE Cordis::QtUiPluginSdk)
set_target_properties(cordis_p7_log_fixture PROPERTIES AUTOMOC ON PREFIX "")
cordis_target_defaults(cordis_p7_log_fixture)

add_executable(cordis_p7_interleaved_logs interleaved_logs.cpp)
target_link_libraries(cordis_p7_interleaved_logs PRIVATE cordis_external_workbench Qt5::Test Ela::WidgetTools)
cordis_target_defaults(cordis_p7_interleaved_logs)
add_dependencies(cordis_p7_interleaved_logs cordis_p7_log_fixture)
add_test(NAME p7.external_interleaved_logs COMMAND cordis_p7_interleaved_logs
    "$<TARGET_FILE:cordis_p7_log_fixture>" "${CMAKE_CURRENT_BINARY_DIR}/artifacts")
set_tests_properties(p7.external_interleaved_logs PROPERTIES LABELS "p7;module;qt;ui;external" TIMEOUT 45
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
