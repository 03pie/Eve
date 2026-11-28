include("${PROJECT_SOURCE_DIR}/cmake/Ela.cmake")
if(EVE_SHARED_CORE)
    # The shared business SDK exports the facades; the official plugin owns the windows.
    add_library(eve_ui SHARED)
    set_target_properties(eve_ui PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON DEBUG_POSTFIX d)
    add_library(eve_desktop_impl STATIC)
    target_link_libraries(eve_desktop_impl PUBLIC eve_ui)
    eve_target_defaults(eve_desktop_impl)
    set(desktop_target eve_desktop_impl)
else()
    # Source-embedded hosts historically acquire the full shell by linking Eve::Ui.
    add_library(eve_ui STATIC)
    add_library(eve_desktop_impl INTERFACE)
    target_link_libraries(eve_desktop_impl INTERFACE eve_ui)
    set(desktop_target eve_ui)
endif()
add_library(Eve::Ui ALIAS eve_ui)
target_sources(eve_ui PRIVATE contributions.cpp runtime_log_archive.cpp runtime_log_retention.cpp runtime_log_query.cpp)
target_link_libraries(eve_ui PUBLIC Eve::Qt Qt5::Widgets)
eve_target_defaults(eve_ui)

target_sources(${desktop_target} PRIVATE pages.cpp services.cpp notifications.cpp management.cpp workbench.cpp workbench_titlebar.cpp
    home.cpp preferences.cpp brand.cpp output.cpp panel.cpp settings_page.cpp settings_controls.cpp extension_dialog.cpp
    extension_readme.cpp extension_features.cpp extension_facts.cpp
    accessible_toggle.cpp output_view.cpp "${PROJECT_SOURCE_DIR}/app/shell/resources/eve_brand.qrc")
set_target_properties(${desktop_target} PROPERTIES AUTORCC ON)
target_link_libraries(${desktop_target} PRIVATE Ela::WidgetTools)

if(TARGET eve_qt_plugin_sdk)
    # Plugins receive the UI/Qt SDK, never the host's module directory or configuration driver.
    add_library(eve_qt_ui_plugin_sdk INTERFACE)
    add_library(Eve::QtUiPluginSdk ALIAS eve_qt_ui_plugin_sdk)
    target_link_libraries(eve_qt_ui_plugin_sdk INTERFACE Eve::QtPluginSdk Eve::Ui Ela::WidgetTools)
    add_library(eve_desktop_sdk INTERFACE)
    add_library(Eve::DesktopSdk ALIAS eve_desktop_sdk)
    target_link_libraries(eve_desktop_sdk INTERFACE Eve::QtUiPluginSdk)
endif()

add_library(eve_config_ui STATIC configuration.cpp configuration_management.cpp)
add_library(Eve::ConfigUi ALIAS eve_config_ui)
target_link_libraries(eve_config_ui PUBLIC eve_desktop_impl Eve::ConfigSession PRIVATE Eve::ConfigIoQt Ela::WidgetTools)
eve_target_defaults(eve_config_ui)

if(TARGET eve_qt_plugin_sdk)
    add_subdirectory(desktop)
endif()
