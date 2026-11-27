include("${PROJECT_SOURCE_DIR}/cmake/Ela.cmake")
# Keep page ownership, platform state and resources in one runtime shared with UI plugins.
set(eve_ui_type STATIC)
if(EVE_SHARED_CORE)
    set(eve_ui_type SHARED)
endif()
add_library(eve_ui ${eve_ui_type} pages.cpp services.cpp notifications.cpp management.cpp workbench.cpp home.cpp preferences.cpp brand.cpp output.cpp panel.cpp
    settings_page.cpp settings_controls.cpp accessible_toggle.cpp runtime_log_archive.cpp runtime_log_retention.cpp runtime_log_query.cpp
    output_view.cpp "${PROJECT_SOURCE_DIR}/app/shell/resources/eve_brand.qrc")
set_target_properties(eve_ui PROPERTIES AUTORCC ON)
add_library(Eve::Ui ALIAS eve_ui)
target_link_libraries(eve_ui PUBLIC Eve::Qt Qt5::Widgets PRIVATE Ela::WidgetTools)
eve_target_defaults(eve_ui)
if(EVE_SHARED_CORE)
    set_target_properties(eve_ui PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON DEBUG_POSTFIX d)
endif()

if(TARGET eve_qt_plugin_sdk)
    # Plugins receive the UI/Qt SDK, never the host's module directory or configuration driver.
    add_library(eve_qt_ui_plugin_sdk INTERFACE)
    add_library(Eve::QtUiPluginSdk ALIAS eve_qt_ui_plugin_sdk)
    target_link_libraries(eve_qt_ui_plugin_sdk INTERFACE Eve::QtPluginSdk Eve::Ui Ela::WidgetTools)
endif()

add_library(eve_config_ui STATIC configuration.cpp configuration_management.cpp)
add_library(Eve::ConfigUi ALIAS eve_config_ui)
target_link_libraries(eve_config_ui PUBLIC Eve::Ui Eve::ConfigSession PRIVATE Eve::ConfigIoQt Ela::WidgetTools)
eve_target_defaults(eve_config_ui)
