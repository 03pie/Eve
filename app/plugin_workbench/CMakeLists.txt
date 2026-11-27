add_library(eve_external_workbench STATIC workbench.cpp)
target_include_directories(eve_external_workbench PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${PROJECT_SOURCE_DIR}/src/ui" "${PROJECT_SOURCE_DIR}/src/module")
target_link_libraries(eve_external_workbench PUBLIC Eve::ModuleQt Eve::ModuleQtPreflight
    Eve::Ui Eve::ConfigSession Eve::ConfigKernel Eve::ConfigStoreQt)
add_dependencies(eve_external_workbench eve_desktop)
eve_target_defaults(eve_external_workbench)
target_compile_definitions(eve_external_workbench PRIVATE EVE_PRODUCT_VERSION="${PROJECT_VERSION}")
add_executable(eve_plugin_workbench WIN32 main.cpp)
target_link_libraries(eve_plugin_workbench PRIVATE eve_external_workbench Qt5::WinMain)
eve_target_defaults(eve_plugin_workbench)
eve_attach_icon(eve_plugin_workbench)

add_executable(eve_log_query log_query.cpp)
target_include_directories(eve_log_query PRIVATE "${PROJECT_SOURCE_DIR}/src/ui")
target_link_libraries(eve_log_query PRIVATE Eve::Ui)
eve_target_defaults(eve_log_query)
