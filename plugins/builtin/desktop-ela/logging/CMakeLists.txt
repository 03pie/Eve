add_library(eve_desktop_logging STATIC desktop_logs.cpp runtime_log_archive.cpp runtime_log_query.cpp runtime_log_retention.cpp)
target_include_directories(eve_desktop_logging PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    "${desktop_contracts}")
target_link_libraries(eve_desktop_logging PUBLIC Eve::Core Qt5::Core)
eve_target_defaults(eve_desktop_logging)

if(TARGET eve_qt_ui_plugin_sdk)
    add_executable(eve_log_query log_query.cpp)
    target_link_libraries(eve_log_query PRIVATE eve_desktop_logging)
    eve_target_defaults(eve_log_query)
endif()
