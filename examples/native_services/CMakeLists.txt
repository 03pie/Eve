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
if(CORDIS_BUILD_EXAMPLES)
    set(demo cordis_native_services_demo)
else()
    set(demo cordis_native_services_probe)
endif()
add_executable(${demo} host.cpp)
target_link_libraries(${demo} PRIVATE Cordis::ModuleHost Cordis::ModuleNative Cordis::ConfigKernel
    Cordis::ConfigSession Cordis::ConfigStoreQt Cordis::Qt)
set_target_properties(${demo} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
cordis_target_defaults(${demo})
add_dependencies(${demo} cordis_example_counter cordis_example_reporter)
if(BUILD_TESTING)
    foreach(case IN ITEMS normal multi realms start_failure cleanup_failure)
        add_test(NAME p4.native_qt_${case} COMMAND ${demo}
            "${CMAKE_BINARY_DIR}/bin/native-services" "${case}")
        set_tests_properties(p4.native_qt_${case} PROPERTIES LABELS "p4;module;native;qt;integration" TIMEOUT 20)
    endforeach()
endif()
