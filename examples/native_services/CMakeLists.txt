foreach(name IN ITEMS counter reporter)
    add_library(cordis_example_${name} MODULE ${name}.cpp support.hpp)
    target_include_directories(cordis_example_${name} PRIVATE "${PROJECT_SOURCE_DIR}/sdk/include")
    target_compile_definitions(cordis_example_${name} PRIVATE CORDIS_MODULE_EXPORTS CORDIS_ABI_EXPORTS)
    cordis_target_defaults(cordis_example_${name})
    set_target_properties(cordis_example_${name} PROPERTIES PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/native-services/${name}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/native-services/${name}")
    set_property(TARGET cordis_example_${name} APPEND PROPERTY LINK_DEPENDS
        "${PROJECT_SOURCE_DIR}/cmake/WriteModuleManifest.cmake")
    add_custom_command(TARGET cordis_example_${name} POST_BUILD
        BYPRODUCTS "${CMAKE_BINARY_DIR}/bin/native-services/${name}/module.json"
        COMMAND "${CMAKE_COMMAND}" "-DLIBRARY=$<TARGET_FILE:cordis_example_${name}>"
            "-DMODULE_ID=org.cordis.example.${name}" "-DPLUGIN_ID=example.${name}"
            -P "${PROJECT_SOURCE_DIR}/cmake/WriteModuleManifest.cmake" VERBATIM)
endforeach()
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/bin/native-services")
configure_file(pipeline.yaml "${CMAKE_BINARY_DIR}/bin/native-services/pipeline.yaml" COPYONLY)
add_library(cordis_native_pipeline STATIC pipeline.cpp pipeline.hpp)
target_link_libraries(cordis_native_pipeline PUBLIC Cordis::ModuleHost Cordis::ModuleNative Cordis::ConfigKernel
    Cordis::ConfigSession Cordis::ConfigStoreQt Cordis::Qt)
target_include_directories(cordis_native_pipeline PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
cordis_target_defaults(cordis_native_pipeline)
add_dependencies(cordis_native_pipeline cordis_example_counter cordis_example_reporter)
if(CORDIS_BUILD_EXAMPLES)
    set(demo cordis_native_services_demo)
else()
    set(demo cordis_native_services_probe)
endif()
add_executable(${demo} host.cpp)
target_link_libraries(${demo} PRIVATE cordis_native_pipeline)
set_target_properties(${demo} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
cordis_target_defaults(${demo})
if(BUILD_TESTING)
    add_executable(cordis_native_pipeline_tests "${PROJECT_SOURCE_DIR}/tests/qt/native_pipeline.cpp")
    target_link_libraries(cordis_native_pipeline_tests PRIVATE cordis_native_pipeline)
    set_target_properties(cordis_native_pipeline_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    cordis_target_defaults(cordis_native_pipeline_tests)
    foreach(case IN ITEMS empty_close retained_runtime wrong_thread open_failure discarded_close)
        add_test(NAME p5.native_pipeline_${case} COMMAND cordis_native_pipeline_tests
            "${CMAKE_BINARY_DIR}/bin/native-services" "${case}")
        set_tests_properties(p5.native_pipeline_${case} PROPERTIES LABELS "p5;module;native;qt;integration;native_pipeline" TIMEOUT 20)
    endforeach()
    foreach(case IN ITEMS normal multi realms start_failure cleanup_failure)
        add_test(NAME p4.native_qt_${case} COMMAND ${demo}
            "${CMAKE_BINARY_DIR}/bin/native-services" "${case}")
        set_tests_properties(p4.native_qt_${case} PROPERTIES LABELS "p4;module;native;qt;integration" TIMEOUT 20)
    endforeach()
endif()
