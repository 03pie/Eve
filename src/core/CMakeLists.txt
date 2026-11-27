find_package(Threads REQUIRED)
set(eve_core_type STATIC)
if(EVE_SHARED_CORE)
    set(eve_core_type SHARED)
endif()
add_library(eve_core ${eve_core_type}
    replacement_borrow.cpp
    replacement_outputs.cpp
    replacement_inputs.cpp
    build_info.cpp
    fake_scheduler.cpp
    fake_clock.cpp
    effect.cpp
    instance.cpp
    state.cpp
    state_budget.cpp
    kernel.cpp
    service.cpp
    service_notifications.cpp
    service_contract.cpp
    service_plan.cpp
    publication.cpp
    replacement.cpp
    replacement_scope.cpp
    replacement_stage.cpp
    replacement_state.cpp
    replacement_commit.cpp
    replacement_inspect.cpp
)
add_library(Eve::Core ALIAS eve_core)
target_include_directories(eve_core PUBLIC
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/sdk/include>"
)
target_compile_definitions(eve_core PRIVATE EVE_SDK_VERSION="${EVE_SDK_VERSION}")
target_link_libraries(eve_core PUBLIC Threads::Threads)
eve_target_defaults(eve_core)

if(EVE_SHARED_CORE)
    # All current SDK out-of-line APIs are functions, not imported global data.
    # This remains an exact-toolchain source SDK, not a stable public binary ABI.
    set_target_properties(eve_core PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON DEBUG_POSTFIX d)
    target_compile_definitions(eve_core PRIVATE EVE_CORE_SHARED=1)
endif()
