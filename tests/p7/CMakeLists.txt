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
foreach(case IN ITEMS waiting multi diagnostics persistence upgrade close_busy)
    add_test(NAME p7.external_${case} COMMAND cordis_p7_acceptance "${case}"
        "${P7_READER_V1_MANIFEST}" "${P7_READER_V2_MANIFEST}" "${CMAKE_CURRENT_BINARY_DIR}/artifacts")
    set_tests_properties(p7.external_${case} PROPERTIES LABELS "p7;module;qt;ui;external" TIMEOUT 180
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()
add_test(NAME p7.external_template COMMAND cordis_p7_acceptance template
    "${P7_TEMPLATE_MANIFEST}" "${P7_TEMPLATE_MANIFEST}" "${CMAKE_CURRENT_BINARY_DIR}/artifacts")
set_tests_properties(p7.external_template PROPERTIES LABELS "p7;module;qt;ui;external" TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
