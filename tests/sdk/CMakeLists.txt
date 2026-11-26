add_executable(cordis_sdk_consumer_tests installed_consumer.cpp)
target_link_libraries(cordis_sdk_consumer_tests PRIVATE cordis_external_workbench Qt5::Test)
cordis_target_defaults(cordis_sdk_consumer_tests)

add_test(NAME sdk.manifest_identity COMMAND "${CMAKE_COMMAND}"
    "-DCORDIS_CMAKE_DIR=${PROJECT_SOURCE_DIR}/cmake" -P "${CMAKE_CURRENT_SOURCE_DIR}/manifest_identity.cmake")
set_tests_properties(sdk.manifest_identity PROPERTIES LABELS sdk)

add_test(NAME sdk.installed_qt_consumer COMMAND "${CMAKE_COMMAND}"
    "-DHOST_BUILD=${CMAKE_BINARY_DIR}"
    "-DHOST_GENERATOR=${CMAKE_GENERATOR}"
    "-DHOST_CONFIG=$<CONFIG>"
    "-DHOST_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
    "-DHOST_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
    "-DHOST_QT_DIR=${Qt5_DIR}"
    "-DTEST_EXE=$<TARGET_FILE:cordis_sdk_consumer_tests>"
    -P "${CMAKE_CURRENT_BINARY_DIR}/installed_consumer.cmake")
set_tests_properties(sdk.installed_qt_consumer PROPERTIES LABELS "sdk;module;qt;ui" TIMEOUT 180
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
configure_file(installed_consumer.cmake.in installed_consumer.cmake @ONLY)
