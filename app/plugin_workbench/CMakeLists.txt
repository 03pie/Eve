add_library(cordis_external_workbench STATIC workbench.cpp)
target_include_directories(cordis_external_workbench PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cordis_external_workbench PUBLIC Cordis::ModuleQt Cordis::ModuleQtPreflight
    Cordis::ConfigUi Cordis::ConfigKernel Cordis::ConfigStoreQt)
cordis_target_defaults(cordis_external_workbench)
add_executable(cordis_plugin_workbench WIN32 main.cpp)
target_link_libraries(cordis_plugin_workbench PRIVATE cordis_external_workbench Qt5::WinMain)
cordis_target_defaults(cordis_plugin_workbench)
cordis_attach_eve_icon(cordis_plugin_workbench)
