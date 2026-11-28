include("${PROJECT_SOURCE_DIR}/cmake/Ela.cmake")
if(EVE_SHARED_CORE)
    # The shared business SDK exports the facades; the official plugin owns the windows.
    add_library(eve_ui SHARED)
    set_target_properties(eve_ui PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON DEBUG_POSTFIX d)
    # CMake's MSVC .def generator uses object timestamps when the exported source list shrinks.
    set_property(SOURCE contributions.cpp APPEND PROPERTY OBJECT_DEPENDS "${CMAKE_CURRENT_LIST_FILE}")
else()
    add_library(eve_ui STATIC)
endif()
add_library(Eve::Ui ALIAS eve_ui)
target_sources(eve_ui PRIVATE contributions.cpp)
target_link_libraries(eve_ui PUBLIC Eve::Qt Qt5::Widgets)
eve_target_defaults(eve_ui)

if(TARGET eve_qt_plugin_sdk)
    # Plugins receive the UI/Qt SDK, never the host's module directory or configuration driver.
    add_library(eve_qt_ui_plugin_sdk INTERFACE)
    add_library(Eve::QtUiPluginSdk ALIAS eve_qt_ui_plugin_sdk)
    target_link_libraries(eve_qt_ui_plugin_sdk INTERFACE Eve::QtPluginSdk Eve::Ui Ela::WidgetTools)
    add_library(eve_desktop_sdk INTERFACE)
    add_library(Eve::DesktopSdk ALIAS eve_desktop_sdk)
    target_link_libraries(eve_desktop_sdk INTERFACE Eve::QtUiPluginSdk)
endif()
