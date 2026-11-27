include("${PROJECT_SOURCE_DIR}/cmake/QtPluginContract.cmake")
eve_qt_toolchain_json(desktop_toolchain)
set(desktop_metadata "${CMAKE_CURRENT_BINARY_DIR}/metadata/$<CONFIG>")
set(desktop_directory "${CMAKE_BINARY_DIR}/bin/system/desktop")
set(desktop_host "${PROJECT_SOURCE_DIR}/app/plugin_workbench")
set(desktop_id "eve.desktop")
set(desktop_module "org.eve.desktop")
file(GENERATE OUTPUT "${desktop_metadata}/desktop_metadata.json"
    CONTENT "{\"moduleId\":\"${desktop_module}\",\"moduleVersion\":\"${PROJECT_VERSION}\",\"plugins\":[\"${desktop_id}\"],\"toolchain\":${desktop_toolchain}}")

# moc embeds the generated JSON into the DLL. Recompile it when the contract changes, even if
# the factory header stayed identical between two releases built in the same directory.
set(desktop_factory "${CMAKE_CURRENT_SOURCE_DIR}/factory_p.hpp")
set(desktop_moc "${desktop_metadata}/moc_factory_p.cpp")
set(desktop_moc_includes "-I${desktop_metadata}" "-I${desktop_host}" "-I${PROJECT_SOURCE_DIR}/sdk/include")
foreach(directory IN LISTS Qt5Core_INCLUDE_DIRS)
    list(APPEND desktop_moc_includes "-I${directory}")
endforeach()
add_custom_command(OUTPUT "${desktop_moc}"
    COMMAND Qt5::moc ${desktop_moc_includes} --output-dep-file "${desktop_factory}" -o "${desktop_moc}"
    DEPENDS "${desktop_factory}" "${desktop_metadata}/desktop_metadata.json"
    DEPFILE "${desktop_moc}.d" VERBATIM)
add_library(eve_desktop MODULE "${desktop_moc}" "${desktop_host}/desktop_activation.cpp")
target_include_directories(eve_desktop PRIVATE "${desktop_host}" "${PROJECT_SOURCE_DIR}/src/ui")
target_link_libraries(eve_desktop PRIVATE Eve::QtPluginSdk Eve::ConfigUi)
set_target_properties(eve_desktop PROPERTIES PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${desktop_directory}" RUNTIME_OUTPUT_DIRECTORY "${desktop_directory}")
eve_target_defaults(eve_desktop)

# Hash the final image, using the same manifest writer as independently built modules.
set(desktop_manifest_writer "${PROJECT_SOURCE_DIR}/cmake/WriteModuleManifest.cmake")
set_property(TARGET eve_desktop APPEND PROPERTY LINK_DEPENDS "${desktop_manifest_writer}")
add_custom_command(TARGET eve_desktop POST_BUILD BYPRODUCTS "${desktop_directory}/module.json"
    COMMAND "${CMAKE_COMMAND}" "-DLIBRARY=$<TARGET_FILE:eve_desktop>" "-DMODULE_ID=${desktop_module}"
        "-DPLUGIN_ID=${desktop_id}" "-DMODULE_VERSION=${PROJECT_VERSION}" -DLOADER=qt-ui -DUNLOAD_POLICY=resident
        -P "${desktop_manifest_writer}" VERBATIM)
