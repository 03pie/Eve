include("${PROJECT_SOURCE_DIR}/cmake/QtPluginContract.cmake")
eve_qt_toolchain_json(desktop_toolchain)
set(desktop_metadata "${CMAKE_CURRENT_BINARY_DIR}/metadata/${PROJECT_VERSION}/${EVE_SDK_VERSION}/$<CONFIG>")
set(desktop_directory "${CMAKE_BINARY_DIR}/bin/system/desktop")
set(desktop_host "${PROJECT_SOURCE_DIR}/app/plugin_workbench")
set(desktop_id "eve.desktop")
set(desktop_module "org.eve.desktop")
file(GENERATE OUTPUT "${desktop_metadata}/desktop_metadata.json"
    CONTENT "{\"moduleId\":\"${desktop_module}\",\"moduleVersion\":\"${PROJECT_VERSION}\",\"plugins\":[\"${desktop_id}\"],\"toolchain\":${desktop_toolchain}}")

# AUTOMOC compiles the factory metadata in its own generated translation unit.
add_library(eve_desktop MODULE factory_p.hpp "${desktop_host}/desktop_activation.cpp")
target_include_directories(eve_desktop PRIVATE "${desktop_host}" "${PROJECT_SOURCE_DIR}/src/ui" "${desktop_metadata}")
target_link_libraries(eve_desktop PRIVATE Eve::QtPluginSdk Eve::ConfigUi)
set_target_properties(eve_desktop PROPERTIES AUTOMOC ON PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${desktop_directory}" RUNTIME_OUTPUT_DIRECTORY "${desktop_directory}")
eve_target_defaults(eve_desktop)

# Hash the final image, using the same manifest writer as independently built modules.
set(desktop_manifest_writer "${PROJECT_SOURCE_DIR}/cmake/WriteModuleManifest.cmake")
set_property(TARGET eve_desktop APPEND PROPERTY LINK_DEPENDS "${desktop_manifest_writer}")
add_custom_command(TARGET eve_desktop POST_BUILD BYPRODUCTS "${desktop_directory}/module.json"
    COMMAND "${CMAKE_COMMAND}" "-DLIBRARY=$<TARGET_FILE:eve_desktop>" "-DMODULE_ID=${desktop_module}"
        "-DPLUGIN_ID=${desktop_id}" "-DMODULE_VERSION=${PROJECT_VERSION}" -DLOADER=qt-ui -DUNLOAD_POLICY=resident
        -P "${desktop_manifest_writer}" VERBATIM)
