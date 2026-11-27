include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/QtPluginContract.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/ModuleIdentity.cmake")

# One QObject factory translation unit; optional implementation sources remain ordinary C++.
function(eve_add_qt_ui_plugin target)
    set(manifest_writer "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/WriteModuleManifest.cmake")
    cmake_parse_arguments(PARSE_ARGV 1 plugin "" "SOURCE;MODULE_ID;PLUGIN_ID;MODULE_VERSION" "SOURCES")
    if(DEFINED plugin_UNPARSED_ARGUMENTS OR DEFINED plugin_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "eve_add_qt_ui_plugin received unknown or empty arguments")
    endif()
    foreach(key IN ITEMS SOURCE MODULE_ID PLUGIN_ID MODULE_VERSION)
        if(NOT DEFINED plugin_${key} OR "${plugin_${key}}" STREQUAL "")
            message(FATAL_ERROR "eve_add_qt_ui_plugin requires ${key}")
        endif()
    endforeach()
    eve_validate_module_identity("${plugin_MODULE_ID}" "${plugin_PLUGIN_ID}" "${plugin_MODULE_VERSION}")
    if(NOT TARGET Eve::QtUiPluginSdk OR NOT DEFINED Eve_VERSION)
        message(FATAL_ERROR "Call find_package(Eve CONFIG REQUIRED COMPONENTS QtUiPluginSdk) before this helper")
    endif()
    get_filename_component(source "${plugin_SOURCE}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    if(NOT EXISTS "${source}")
        message(FATAL_ERROR "Plugin SOURCE does not exist: ${source}")
    endif()
    get_filename_component(stem "${source}" NAME_WE)
    set(directory "${CMAKE_CURRENT_BINARY_DIR}/eve-plugin/${target}")
    set(EVE_SDK_VERSION "${Eve_VERSION}")
    eve_qt_toolchain_json(toolchain)
    file(GENERATE OUTPUT "${directory}/eve_plugin_metadata.json"
        CONTENT "{\"moduleId\":\"${plugin_MODULE_ID}\",\"moduleVersion\":\"${plugin_MODULE_VERSION}\",\"plugins\":[\"${plugin_PLUGIN_ID}\"],\"toolchain\":${toolchain}}\n")
    set(moc_includes "-I${directory}")
    set(moc_contracts)
    get_target_property(sdk_includes Eve::Core INTERFACE_INCLUDE_DIRECTORIES)
    foreach(path IN LISTS sdk_includes Qt5Core_INCLUDE_DIRS)
        list(APPEND moc_includes "-I${path}")
    endforeach()
    foreach(path IN LISTS sdk_includes)
        list(APPEND moc_contracts "${path}/eve/module/qt_plugin.hpp" "${path}/eve/module/qt_state_plugin.hpp")
    endforeach()
    add_custom_command(OUTPUT "${directory}/${stem}.moc"
        COMMAND Qt5::moc ${moc_includes} "${source}" -o "${directory}/${stem}.moc"
        DEPENDS "${source}" ${plugin_SOURCES} ${moc_contracts} "${directory}/eve_plugin_metadata.json" VERBATIM)
    add_library(${target} MODULE "${source}" ${plugin_SOURCES} "${directory}/${stem}.moc")
    target_compile_definitions(${target} PRIVATE "EVE_QT_UI_PLUGIN_ID=\"${plugin_PLUGIN_ID}\"")
    target_include_directories(${target} PRIVATE "${directory}")
    target_link_libraries(${target} PRIVATE Eve::QtUiPluginSdk)
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    set_target_properties(${target} PROPERTIES PREFIX "" AUTOMOC OFF
        MSVC_RUNTIME_LIBRARY "${Eve_SDK_RUNTIME}"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/module/${target}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/module/${target}")
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${manifest_writer}")
    add_custom_command(TARGET ${target} POST_BUILD
        BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/module/${target}/module.json"
        COMMAND "${CMAKE_COMMAND}" "-DLIBRARY=$<TARGET_FILE:${target}>" "-DMODULE_ID=${plugin_MODULE_ID}"
            "-DPLUGIN_ID=${plugin_PLUGIN_ID}" "-DMODULE_VERSION=${plugin_MODULE_VERSION}"
            -DLOADER=qt-ui -DUNLOAD_POLICY=resident -P "${manifest_writer}" VERBATIM)
endfunction()
