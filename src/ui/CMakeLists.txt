include("${PROJECT_SOURCE_DIR}/cmake/Ela.cmake")
# Keep page ownership, platform state and resources in one runtime shared with UI plugins.
set(cordis_ui_type STATIC)
if(CORDIS_SHARED_CORE)
    set(cordis_ui_type SHARED)
endif()
add_library(cordis_ui ${cordis_ui_type} pages.cpp services.cpp management.cpp workbench.cpp home.cpp preferences.cpp brand.cpp output.cpp panel.cpp
    "${PROJECT_SOURCE_DIR}/app/shell/resources/eve_brand.qrc")
set_target_properties(cordis_ui PROPERTIES AUTORCC ON)
add_library(Cordis::Ui ALIAS cordis_ui)
target_link_libraries(cordis_ui PUBLIC Cordis::Qt Qt5::Widgets PRIVATE Ela::WidgetTools)
cordis_target_defaults(cordis_ui)
if(CORDIS_SHARED_CORE)
    set_target_properties(cordis_ui PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON DEBUG_POSTFIX d)
endif()

if(TARGET cordis_qt_plugin_sdk)
    # Plugins receive the UI/Qt SDK, never the host's module directory or configuration driver.
    add_library(cordis_qt_ui_plugin_sdk INTERFACE)
    add_library(Cordis::QtUiPluginSdk ALIAS cordis_qt_ui_plugin_sdk)
    target_link_libraries(cordis_qt_ui_plugin_sdk INTERFACE Cordis::QtPluginSdk Cordis::Ui Ela::WidgetTools)
endif()

add_library(cordis_config_ui STATIC configuration.cpp configuration_management.cpp)
add_library(Cordis::ConfigUi ALIAS cordis_config_ui)
target_link_libraries(cordis_config_ui PUBLIC Cordis::Ui Cordis::ConfigSession PRIVATE Cordis::ConfigIoQt Ela::WidgetTools)
cordis_target_defaults(cordis_config_ui)
