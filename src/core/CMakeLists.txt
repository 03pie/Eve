find_package(Threads REQUIRED)
set(cordis_core_type STATIC)
if(CORDIS_SHARED_CORE)
    set(cordis_core_type SHARED)
endif()
add_library(cordis_core ${cordis_core_type} build_info.cpp fake_scheduler.cpp fake_clock.cpp effect.cpp instance.cpp kernel.cpp service.cpp publication.cpp)
add_library(Cordis::Core ALIAS cordis_core)
target_include_directories(cordis_core PUBLIC
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/sdk/include>"
)
target_compile_definitions(cordis_core PRIVATE CORDIS_PROJECT_VERSION="${PROJECT_VERSION}")
target_link_libraries(cordis_core PUBLIC Threads::Threads)
cordis_target_defaults(cordis_core)

if(CORDIS_SHARED_CORE)
    # All current SDK out-of-line APIs are functions, not imported global data.
    # This remains an exact-toolchain source SDK, not a stable public binary ABI.
    set_target_properties(cordis_core PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON DEBUG_POSTFIX d)
    target_compile_definitions(cordis_core PRIVATE CORDIS_CORE_SHARED=1)
endif()
