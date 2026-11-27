function(eve_add_native_service_example target name directory version unload_policy)
    cmake_parse_arguments(SERVICE "NO_DISCOVERY;NO_BUSINESS" "SOURCE" "" ${ARGN})
    set(service_source "${name}.cpp")
    if(SERVICE_SOURCE)
        set(service_source "${SERVICE_SOURCE}")
    endif()
    set(service_exports)
    if(NOT SERVICE_NO_DISCOVERY)
        list(APPEND service_exports EVE_MODULE_EXPORTS)
    endif()
    if(NOT SERVICE_NO_BUSINESS)
        list(APPEND service_exports EVE_ABI_EXPORTS)
    endif()
    add_library(${target} MODULE "${service_source}" support.hpp)
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/sdk/include")
    target_compile_definitions(${target} PRIVATE ${service_exports}
        EVE_EXAMPLE_MODULE_VERSION="${version}")
    if(unload_policy STREQUAL "explicit")
        target_compile_definitions(${target} PRIVATE EVE_EXAMPLE_EXPLICIT_UNLOAD=1)
        if(name STREQUAL "counter")
            target_compile_definitions(${target} PRIVATE EVE_EXAMPLE_COUNTER_STATE=1)
        endif()
    endif()
    eve_target_defaults(${target})
    set_target_properties(${target} PROPERTIES PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/${directory}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/${directory}")
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
        "${PROJECT_SOURCE_DIR}/cmake/WriteModuleManifest.cmake")
    add_custom_command(TARGET ${target} POST_BUILD
        BYPRODUCTS "${CMAKE_BINARY_DIR}/bin/${directory}/module.json"
        COMMAND "${CMAKE_COMMAND}" "-DLIBRARY=$<TARGET_FILE:${target}>"
            "-DMODULE_ID=org.eve.example.${name}" "-DPLUGIN_ID=example.${name}"
            "-DMODULE_VERSION=${version}" "-DUNLOAD_POLICY=${unload_policy}"
            -P "${PROJECT_SOURCE_DIR}/cmake/WriteModuleManifest.cmake" VERBATIM)
endfunction()
foreach(name IN ITEMS counter reporter)
    eve_add_native_service_example(eve_example_${name} ${name} "native-services/${name}" "1.0.0" resident)
    eve_add_native_service_example(eve_example_${name}_a ${name} "native-upgrade/${name}-a" "1.1.0" explicit)
    eve_add_native_service_example(eve_example_${name}_b ${name} "native-upgrade/${name}-b" "1.2.0" explicit)
endforeach()
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/bin/native-services")
configure_file(pipeline.yaml "${CMAKE_BINARY_DIR}/bin/native-services/pipeline.yaml" COPYONLY)
add_library(eve_native_service_modules STATIC service_module.cpp service_module.hpp)
target_link_libraries(eve_native_service_modules PUBLIC
    Eve::ModuleHost Eve::ModuleNative Eve::ConfigDefinitions)
target_include_directories(eve_native_service_modules PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
eve_target_defaults(eve_native_service_modules)
add_library(eve_native_pipeline STATIC pipeline.cpp pipeline.hpp)
target_link_libraries(eve_native_pipeline PUBLIC eve_native_service_modules Eve::ConfigKernel
    Eve::ConfigSession Eve::ConfigStoreQt Eve::Qt)
target_include_directories(eve_native_pipeline PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
eve_target_defaults(eve_native_pipeline)
add_dependencies(eve_native_pipeline eve_example_counter eve_example_reporter)
if(EVE_BUILD_EXAMPLES)
    set(demo eve_native_services_demo)
else()
    set(demo eve_native_services_probe)
endif()
add_executable(${demo} host.cpp)
target_link_libraries(${demo} PRIVATE eve_native_pipeline)
set_target_properties(${demo} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
eve_target_defaults(${demo})
if(BUILD_TESTING)
    # Reuse the actual business code, with only the intended public entry absent.
    eve_add_native_service_example(eve_example_counter_missing_query counter
        "native-faults/counter-missing-query" "1.3.0" resident NO_DISCOVERY)
    eve_add_native_service_example(eve_example_counter_missing_business counter
        "native-faults/counter-missing-business" "1.4.0" resident NO_BUSINESS)
    foreach(variant IN ITEMS once recovery)
        if(variant STREQUAL "once")
            set(fault_version "1.3.0")
            set(fault_recovery 0)
        else()
            set(fault_version "1.4.0")
            set(fault_recovery 1)
        endif()
        eve_add_native_service_example(eve_example_reporter_fail_${variant} reporter
            "native-faults/reporter-fail-${variant}" "${fault_version}" resident NO_BUSINESS
            SOURCE "${PROJECT_SOURCE_DIR}/tests/fixtures/business_reporter_failure.cpp")
        target_compile_definitions(eve_example_reporter_fail_${variant} PRIVATE
            EVE_EXAMPLE_REPORTER_FAIL_RECOVERY=${fault_recovery})
    endforeach()
    foreach(target IN ITEMS eve_example_counter_missing_query eve_example_counter_missing_business
        eve_example_reporter_fail_once eve_example_reporter_fail_recovery)
        set_target_properties(${target} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS OFF)
    endforeach()
    add_executable(eve_native_service_state_tests
        "${PROJECT_SOURCE_DIR}/tests/qt/native_service_state.cpp"
        "${PROJECT_SOURCE_DIR}/tests/qt/native_service_state_abi.cpp")
    target_link_libraries(eve_native_service_state_tests PRIVATE
        Eve::ModuleHost Eve::ModuleNative Eve::ModulePreflight Eve::Qt)
    set_target_properties(eve_native_service_state_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    eve_target_defaults(eve_native_service_state_tests)
    add_dependencies(eve_native_service_state_tests eve_example_counter eve_example_reporter
        eve_example_counter_a eve_example_counter_b eve_example_reporter_a eve_example_reporter_b)
    foreach(case IN ITEMS abi_discovery abi_state_schema abi_state_values abi_state_buffers
        runtime_a runtime_b runtime_boundaries runtime_rejections)
        add_test(NAME p5.business_state_${case} COMMAND eve_native_service_state_tests "${CMAKE_BINARY_DIR}/bin" "${case}")
        set_tests_properties(p5.business_state_${case} PROPERTIES LABELS "p5;module;native;qt;state;business_state" TIMEOUT 30)
    endforeach()
    add_executable(eve_native_pipeline_tests "${PROJECT_SOURCE_DIR}/tests/qt/native_pipeline.cpp")
    target_link_libraries(eve_native_pipeline_tests PRIVATE eve_native_pipeline)
    set_target_properties(eve_native_pipeline_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    eve_target_defaults(eve_native_pipeline_tests)
    foreach(case IN ITEMS empty_close retained_runtime wrong_thread open_failure discarded_close)
        add_test(NAME p5.native_pipeline_${case} COMMAND eve_native_pipeline_tests
            "${CMAKE_BINARY_DIR}/bin/native-services" "${case}")
        set_tests_properties(p5.native_pipeline_${case} PROPERTIES LABELS "p5;module;native;qt;integration;native_pipeline" TIMEOUT 20)
    endforeach()
    foreach(case IN ITEMS normal multi realms start_failure cleanup_failure)
        add_test(NAME p4.native_qt_${case} COMMAND ${demo}
            "${CMAKE_BINARY_DIR}/bin/native-services" "${case}")
        set_tests_properties(p4.native_qt_${case} PROPERTIES LABELS "p4;module;native;qt;integration" TIMEOUT 20)
    endforeach()
endif()
