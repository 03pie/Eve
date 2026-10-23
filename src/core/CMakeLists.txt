find_package(Threads REQUIRED)
add_library(cordis_core STATIC build_info.cpp fake_scheduler.cpp fake_clock.cpp effect.cpp instance.cpp kernel.cpp service.cpp)
add_library(Cordis::Core ALIAS cordis_core)
target_include_directories(cordis_core PUBLIC
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/sdk/include>"
)
target_compile_definitions(cordis_core PRIVATE CORDIS_PROJECT_VERSION="${PROJECT_VERSION}")
target_link_libraries(cordis_core PUBLIC Threads::Threads)
cordis_target_defaults(cordis_core)
