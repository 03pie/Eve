function(cordis_add_native_service_example target name directory version unload_policy)
    cmake_parse_arguments(SERVICE "NO_DISCOVERY;NO_BUSINESS" "SOURCE" "" ${ARGN})
    set(service_source "${name}.cpp")
    if(SERVICE_SOURCE)
        set(service_source "${SERVICE_SOURCE}")
    endif()
    set(service_exports)
    if(NOT SERVICE_NO_DISCOVERY)
        list(APPEND service_exports CORDIS_MODULE_EXPORTS)
    endif()
    if(NOT SERVICE_NO_BUSINESS)
        list(APPEND service_exports CORDIS_ABI_EXPORTS)
    endif()
    add_library(${target} MODULE "${service_source}" support.hpp)
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/sdk/include")
    target_compile_definitions(${target} PRIVATE ${service_exports}
        CORDIS_EXAMPLE_MODULE_VERSION="${version}")
    if(unload_policy STREQUAL "explicit")
        target_compile_definitions(${target} PRIVATE CORDIS_EXAMPLE_EXPLICIT_UNLOAD=1)
        if(name STREQUAL "counter")
            target_compile_definitions(${target} PRIVATE CORDIS_EXAMPLE_COUNTER_STATE=1)
        endif()
    endif()
    cordis_target_defaults(${target})
    set_target_properties(${target} PROPERTIES PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/${directory}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/${directory}")
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
        "${PROJECT_SOURCE_DIR}/cmake/WriteModuleManifest.cmake")
    add_custom_command(TARGET ${target} POST_BUILD
        BYPRODUCTS "${CMAKE_BINARY_DIR}/bin/${directory}/module.json"
        COMMAND "${CMAKE_COMMAND}" "-DLIBRARY=$<TARGET_FILE:${target}>"
            "-DMODULE_ID=org.cordis.example.${name}" "-DPLUGIN_ID=example.${name}"
            "-DMODULE_VERSION=${version}" "-DUNLOAD_POLICY=${unload_policy}"
            -P "${PROJECT_SOURCE_DIR}/cmake/WriteModuleManifest.cmake" VERBATIM)
endfunction()
foreach(name IN ITEMS counter reporter)
    cordis_add_native_service_example(cordis_example_${name} ${name} "native-services/${name}" "1.0.0" resident)
    cordis_add_native_service_example(cordis_example_${name}_a ${name} "native-upgrade/${name}-a" "1.1.0" explicit)
    cordis_add_native_service_example(cordis_example_${name}_b ${name} "native-upgrade/${name}-b" "1.2.0" explicit)
endforeach()
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/bin/native-services")
configure_file(pipeline.yaml "${CMAKE_BINARY_DIR}/bin/native-services/pipeline.yaml" COPYONLY)
add_library(cordis_native_service_modules STATIC service_module.cpp service_module.hpp)
target_link_libraries(cordis_native_service_modules PUBLIC
    Cordis::ModuleHost Cordis::ModuleNative Cordis::ConfigDefinitions)
target_include_directories(cordis_native_service_modules PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
cordis_target_defaults(cordis_native_service_modules)
add_library(cordis_native_pipeline STATIC pipeline.cpp pipeline.hpp)
target_link_libraries(cordis_native_pipeline PUBLIC cordis_native_service_modules Cordis::ConfigKernel
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
    # Reuse the actual business code, with only the intended public entry absent.
    cordis_add_native_service_example(cordis_example_counter_missing_query counter
        "native-faults/counter-missing-query" "1.3.0" resident NO_DISCOVERY)
    cordis_add_native_service_example(cordis_example_counter_missing_business counter
        "native-faults/counter-missing-business" "1.4.0" resident NO_BUSINESS)
    foreach(variant IN ITEMS once recovery)
        if(variant STREQUAL "once")
            set(fault_version "1.3.0")
            set(fault_recovery 0)
        else()
            set(fault_version "1.4.0")
            set(fault_recovery 1)
        endif()
        cordis_add_native_service_example(cordis_example_reporter_fail_${variant} reporter
            "native-faults/reporter-fail-${variant}" "${fault_version}" resident NO_BUSINESS
            SOURCE "${PROJECT_SOURCE_DIR}/tests/fixtures/business_reporter_failure.cpp")
        target_compile_definitions(cordis_example_reporter_fail_${variant} PRIVATE
            CORDIS_EXAMPLE_REPORTER_FAIL_RECOVERY=${fault_recovery})
    endforeach()
    foreach(target IN ITEMS cordis_example_counter_missing_query cordis_example_counter_missing_business
        cordis_example_reporter_fail_once cordis_example_reporter_fail_recovery)
        set_target_properties(${target} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS OFF)
    endforeach()
    add_executable(cordis_native_service_state_tests
        "${PROJECT_SOURCE_DIR}/tests/qt/native_service_state.cpp"
        "${PROJECT_SOURCE_DIR}/tests/qt/native_service_state_abi.cpp")
    target_link_libraries(cordis_native_service_state_tests PRIVATE
        Cordis::ModuleHost Cordis::ModuleNative Cordis::ModulePreflight Cordis::Qt)
    set_target_properties(cordis_native_service_state_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    cordis_target_defaults(cordis_native_service_state_tests)
    add_dependencies(cordis_native_service_state_tests cordis_example_counter cordis_example_reporter
        cordis_example_counter_a cordis_example_counter_b cordis_example_reporter_a cordis_example_reporter_b)
    foreach(case IN ITEMS abi_discovery abi_state_schema abi_state_values abi_state_buffers
        runtime_a runtime_b runtime_boundaries runtime_rejections)
        add_test(NAME p5.business_state_${case} COMMAND cordis_native_service_state_tests "${CMAKE_BINARY_DIR}/bin" "${case}")
        set_tests_properties(p5.business_state_${case} PROPERTIES LABELS "p5;module;native;qt;state;business_state" TIMEOUT 30)
    endforeach()
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
