add_executable(cordis_foundation main.cpp)
target_link_libraries(cordis_foundation PRIVATE Cordis::Core)
cordis_target_defaults(cordis_foundation)
