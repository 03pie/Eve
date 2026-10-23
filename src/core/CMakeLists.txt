add_library(cordis_core STATIC build_info.cpp)
add_library(Cordis::Core ALIAS cordis_core)
target_include_directories(cordis_core PUBLIC
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/sdk/include>"
)
target_compile_definitions(cordis_core PRIVATE CORDIS_PROJECT_VERSION="${PROJECT_VERSION}")
cordis_target_defaults(cordis_core)
