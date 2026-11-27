include("${PROJECT_SOURCE_DIR}/cmake/QtPluginContract.cmake")
eve_qt_toolchain_json(logging_toolchain)
set(logging_generated "${CMAKE_CURRENT_BINARY_DIR}/metadata/$<CONFIG>")
file(GENERATE OUTPUT "${logging_generated}/logging_metadata.json"
    CONTENT "{\"moduleId\":\"org.eve.test.logging\",\"moduleVersion\":\"1.0.0\",\"plugins\":[\"test.logging\"],\"toolchain\":${logging_toolchain}}")
add_library(eve_logging_fixture MODULE logging_fixture.cpp)
target_include_directories(eve_logging_fixture PRIVATE "${logging_generated}")
target_link_libraries(eve_logging_fixture PRIVATE Eve::QtUiPluginSdk)
set_target_properties(eve_logging_fixture PROPERTIES AUTOMOC ON PREFIX "")
eve_target_defaults(eve_logging_fixture)

add_executable(eve_logging_acceptance runtime_logging.cpp)
target_include_directories(eve_logging_acceptance PRIVATE "${PROJECT_SOURCE_DIR}/src/ui")
target_link_libraries(eve_logging_acceptance PRIVATE eve_external_workbench Qt5::Test)
eve_target_defaults(eve_logging_acceptance)
add_dependencies(eve_logging_acceptance eve_plugin_workbench eve_logging_fixture)

add_test(NAME logging.l1_cross_process COMMAND eve_logging_acceptance "$<TARGET_FILE:eve_logging_fixture>"
    "$<TARGET_FILE:eve_plugin_workbench>" "${CMAKE_CURRENT_BINARY_DIR}/artifacts")
set_tests_properties(logging.l1_cross_process PROPERTIES LABELS "logging;qt;ui" TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_test(NAME logging.l3_close_retry COMMAND eve_logging_acceptance "$<TARGET_FILE:eve_logging_fixture>"
    "$<TARGET_FILE:eve_plugin_workbench>" "${CMAKE_CURRENT_BINARY_DIR}/artifacts" close_retry)
set_tests_properties(logging.l3_close_retry PROPERTIES LABELS "logging;qt;ui" TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(eve_log_retention_tests log_retention.cpp)
target_include_directories(eve_log_retention_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/ui")
target_link_libraries(eve_log_retention_tests PRIVATE Eve::Ui)
eve_target_defaults(eve_log_retention_tests)
add_test(NAME logging.l5_retention COMMAND eve_log_retention_tests "${CMAKE_CURRENT_BINARY_DIR}/artifacts")
set_tests_properties(logging.l5_retention PROPERTIES LABELS "logging;qt;ui" TIMEOUT 45)

add_executable(eve_logging_durability archive_durability.cpp)
target_include_directories(eve_logging_durability PRIVATE "${PROJECT_SOURCE_DIR}/src/ui")
target_link_libraries(eve_logging_durability PRIVATE Eve::Ui)
eve_target_defaults(eve_logging_durability)
foreach(case IN ITEMS short first_short pressure unconfirmed concurrent_close)
    add_test(NAME logging.l3_${case} COMMAND eve_logging_durability "${case}")
    set_tests_properties(logging.l3_${case} PROPERTIES LABELS "logging;qt" TIMEOUT 30)
endforeach()

add_library(eve_native_logging_fixture MODULE native_logging_fixture.c)
target_include_directories(eve_native_logging_fixture PRIVATE "${PROJECT_SOURCE_DIR}/sdk/include")
target_compile_definitions(eve_native_logging_fixture PRIVATE EVE_MODULE_EXPORTS EVE_ABI_EXPORTS)
set_target_properties(eve_native_logging_fixture PROPERTIES PREFIX "" C_STANDARD 11 C_STANDARD_REQUIRED ON)
eve_target_defaults(eve_native_logging_fixture)

add_executable(eve_native_logging_tests native_logging.cpp)
target_link_libraries(eve_native_logging_tests PRIVATE Eve::ModuleHost Eve::ModuleNative Qt5::Core)
eve_target_defaults(eve_native_logging_tests)
add_dependencies(eve_native_logging_tests eve_native_logging_fixture)
foreach(case IN ITEMS boundary publication)
    add_test(NAME logging.l4_native_${case} COMMAND eve_native_logging_tests
        "${case}" "$<TARGET_FILE:eve_native_logging_fixture>")
    set_tests_properties(logging.l4_native_${case} PROPERTIES LABELS "logging;native;abi" TIMEOUT 20)
endforeach()

add_executable(eve_native_logging_archive_tests native_logging_archive.cpp)
target_include_directories(eve_native_logging_archive_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/ui")
target_link_libraries(eve_native_logging_archive_tests PRIVATE
    Eve::ModuleHost Eve::ModuleNative Eve::Ui Qt5::Test)
eve_target_defaults(eve_native_logging_archive_tests)
add_dependencies(eve_native_logging_archive_tests eve_native_logging_fixture)
add_test(NAME logging.l4_native_archive COMMAND eve_native_logging_archive_tests
    "$<TARGET_FILE:eve_native_logging_fixture>" "${CMAKE_CURRENT_BINARY_DIR}/artifacts")
set_tests_properties(logging.l4_native_archive PROPERTIES LABELS "logging;native;abi;qt;ui" TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
if(WIN32)
    target_link_libraries(eve_logging_durability PRIVATE Advapi32)
    add_test(NAME logging.l3_inaccessible COMMAND eve_logging_durability inaccessible)
    set_tests_properties(logging.l3_inaccessible PROPERTIES LABELS "logging;qt" TIMEOUT 30)
endif()

add_executable(eve_logging_query log_query.cpp)
target_include_directories(eve_logging_query PRIVATE "${PROJECT_SOURCE_DIR}/src/ui")
target_link_libraries(eve_logging_query PRIVATE Eve::Ui)
eve_target_defaults(eve_logging_query)
add_dependencies(eve_logging_query eve_log_query)
add_test(NAME logging.l6_query COMMAND eve_logging_query "${CMAKE_CURRENT_BINARY_DIR}/artifacts"
    "$<TARGET_FILE:eve_log_query>")
set_tests_properties(logging.l6_query PROPERTIES LABELS "logging;qt" TIMEOUT 30)

add_executable(eve_platform_startup_tests platform_startup.cpp)
target_include_directories(eve_platform_startup_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/ui")
target_link_libraries(eve_platform_startup_tests PRIVATE eve_external_workbench Qt5::Test)
eve_target_defaults(eve_platform_startup_tests)
add_test(NAME platform.empty_workbench COMMAND eve_platform_startup_tests)
set_tests_properties(platform.empty_workbench PROPERTIES LABELS "platform;qt;ui" TIMEOUT 35
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

file(GENERATE OUTPUT "${logging_generated}/interleaved_metadata.json"
    CONTENT "{\"moduleId\":\"org.eve.test.logs\",\"moduleVersion\":\"1.0.0\",\"plugins\":[\"test.logs.alpha\",\"test.logs.bravo\"],\"toolchain\":${logging_toolchain}}")
add_library(eve_interleaved_logging_fixture MODULE interleaved_fixture.cpp)
target_include_directories(eve_interleaved_logging_fixture PRIVATE "${logging_generated}")
target_link_libraries(eve_interleaved_logging_fixture PRIVATE Eve::QtUiPluginSdk)
set_target_properties(eve_interleaved_logging_fixture PROPERTIES AUTOMOC ON PREFIX "")
eve_target_defaults(eve_interleaved_logging_fixture)

add_executable(eve_interleaved_logging_tests interleaved_logs.cpp)
target_link_libraries(eve_interleaved_logging_tests PRIVATE eve_external_workbench Qt5::Test Ela::WidgetTools)
eve_target_defaults(eve_interleaved_logging_tests)
add_dependencies(eve_interleaved_logging_tests eve_interleaved_logging_fixture)
add_test(NAME logging.interleaved_sources COMMAND eve_interleaved_logging_tests
    "$<TARGET_FILE:eve_interleaved_logging_fixture>" "${CMAKE_CURRENT_BINARY_DIR}/artifacts")
set_tests_properties(logging.interleaved_sources PROPERTIES LABELS "logging;module;qt;ui" TIMEOUT 45
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
