add_subdirectory(catalog)
if(NOT TARGET eve_qt_ui_plugin_sdk)
    return()
endif()

add_library(eve_workspace_extensions STATIC workspace_extensions.cpp)
target_include_directories(eve_workspace_extensions PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/contracts/workbench")
target_link_libraries(eve_workspace_extensions PUBLIC Eve::Extensions Eve::ConfigKernel Eve::ConfigSession Eve::Ui
    PRIVATE Eve::ModuleQt)
eve_target_defaults(eve_workspace_extensions)
