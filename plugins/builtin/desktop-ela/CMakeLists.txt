cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(EveElaDesktop VERSION 0.3.1 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    if(MSVC AND EVE_MSVC_SHOWINCLUDES_PREFIX)
        set(CMAKE_CL_SHOWINCLUDES_PREFIX "${EVE_MSVC_SHOWINCLUDES_PREFIX}")
    endif()
    find_package(EveWorkbench ${PROJECT_VERSION} EXACT CONFIG REQUIRED)
    set(desktop_contracts "${EveWorkbench_PRIVATE_INCLUDE_DIR}")
    set(desktop_backends "${EveWorkbench_PRIVATE_INCLUDE_DIR}")
    set(desktop_brand "${EveWorkbench_BRAND_QRC}")
    set(desktop_cmake "${EveWorkbench_CMAKE_DIR}")
    set(desktop_config_session EveWorkbench::ConfigSession)
    set(desktop_config_io EveWorkbench::ConfigIoQt)
    set(EVE_SDK_VERSION "${Eve_VERSION}")
    set(EVE_SHARED_CORE ON)
    function(eve_target_defaults target)
        target_compile_features(${target} PUBLIC cxx_std_17)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    endfunction()
else()
    set(desktop_contracts "${PROJECT_SOURCE_DIR}/contracts/workbench")
    set(desktop_backends "${PROJECT_SOURCE_DIR}/src/ui")
    set(desktop_brand "${PROJECT_SOURCE_DIR}/app/shell/resources/eve_brand.qrc")
    set(desktop_cmake "${PROJECT_SOURCE_DIR}/cmake")
    set(desktop_config_session Eve::ConfigSession)
    set(desktop_config_io Eve::ConfigIoQt)
endif()

add_subdirectory(logging)
if(EVE_SHARED_CORE)
    add_library(eve_desktop_impl STATIC)
    target_link_libraries(eve_desktop_impl PUBLIC Eve::Ui)
    eve_target_defaults(eve_desktop_impl)
    set(desktop_target eve_desktop_impl)
else()
    # Source-embedded hosts historically acquire the full shell by linking Eve::Ui.
    add_library(eve_desktop_impl INTERFACE)
    target_link_libraries(eve_desktop_impl INTERFACE eve_ui)
    set(desktop_target eve_ui)
endif()
target_sources(${desktop_target} PRIVATE
    pages.cpp services.cpp notifications.cpp management.cpp workbench.cpp workbench_titlebar.cpp
    home.cpp preferences.cpp desktop_language.cpp brand.cpp output.cpp panel.cpp settings_page.cpp settings_controls.cpp
    extension_dialog.cpp extension_readme.cpp extension_features.cpp extension_facts.cpp
    accessible_toggle.cpp output_view.cpp "${desktop_brand}")
set_target_properties(${desktop_target} PROPERTIES AUTORCC ON)
target_include_directories(${desktop_target} PRIVATE "${desktop_contracts}" "${desktop_backends}")
target_link_libraries(${desktop_target} PRIVATE eve_desktop_logging Ela::WidgetTools)

set(desktop_directory "${CMAKE_BINARY_DIR}/bin/system/desktop")
set(desktop_messages "${CMAKE_CURRENT_SOURCE_DIR}/i18n/messages.en.json")
add_custom_command(OUTPUT "${desktop_directory}/messages.en.json"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${desktop_directory}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${desktop_messages}" "${desktop_directory}/messages.en.json"
    DEPENDS "${desktop_messages}" VERBATIM)
add_custom_target(eve_desktop_messages DEPENDS "${desktop_directory}/messages.en.json")
add_dependencies(eve_desktop_logging eve_desktop_messages)

add_library(eve_config_ui STATIC configuration.cpp configuration_management.cpp)
add_library(Eve::ConfigUi ALIAS eve_config_ui)
target_include_directories(eve_config_ui PRIVATE "${desktop_contracts}")
target_link_libraries(eve_config_ui PUBLIC eve_desktop_impl ${desktop_config_session} PRIVATE ${desktop_config_io} Ela::WidgetTools)
eve_target_defaults(eve_config_ui)

if(NOT TARGET Eve::QtPluginSdk)
    return()
endif()
include("${desktop_cmake}/QtPluginContract.cmake")
eve_qt_toolchain_json(desktop_toolchain)
set(desktop_metadata "${CMAKE_CURRENT_BINARY_DIR}/metadata/$<CONFIG>")
set(desktop_id "eve.desktop")
set(desktop_module "org.eve.desktop")
file(GENERATE OUTPUT "${desktop_metadata}/desktop_metadata.json"
    CONTENT "{\"moduleId\":\"${desktop_module}\",\"moduleVersion\":\"${PROJECT_VERSION}\",\"plugins\":[\"${desktop_id}\"],\"toolchain\":${desktop_toolchain}}")

# moc embeds the generated JSON into the DLL. Recompile it when the contract changes, even if
# the factory header stayed identical between two releases built in the same directory.
set(desktop_factory "${CMAKE_CURRENT_SOURCE_DIR}/factory_p.hpp")
set(desktop_moc "${desktop_metadata}/moc_factory_p.cpp")
set(desktop_moc_includes "-I${desktop_metadata}" "-I${desktop_contracts}")
get_target_property(desktop_sdk_include_dirs Eve::Core INTERFACE_INCLUDE_DIRECTORIES)
foreach(directory IN LISTS desktop_sdk_include_dirs Qt5Core_INCLUDE_DIRS)
    list(APPEND desktop_moc_includes "-I${directory}")
endforeach()
add_custom_command(OUTPUT "${desktop_moc}"
    COMMAND Qt5::moc ${desktop_moc_includes} --output-dep-file "${desktop_factory}" -o "${desktop_moc}"
    DEPENDS "${desktop_factory}" "${desktop_metadata}/desktop_metadata.json"
    DEPFILE "${desktop_moc}.d" VERBATIM)
add_library(eve_desktop MODULE "${desktop_moc}" desktop_activation.cpp)
target_compile_definitions(eve_desktop PRIVATE EVE_PRODUCT_VERSION="${PROJECT_VERSION}")
target_include_directories(eve_desktop PRIVATE "${desktop_contracts}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(eve_desktop PRIVATE Eve::QtPluginSdk Eve::ConfigUi eve_desktop_logging)
set_target_properties(eve_desktop PROPERTIES PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${desktop_directory}" RUNTIME_OUTPUT_DIRECTORY "${desktop_directory}")
eve_target_defaults(eve_desktop)

# Hash the final image, using the same manifest writer as independently built modules.
set(desktop_manifest_writer "${desktop_cmake}/WriteModuleManifest.cmake")
set_property(TARGET eve_desktop APPEND PROPERTY LINK_DEPENDS "${desktop_manifest_writer}")
add_custom_command(TARGET eve_desktop POST_BUILD BYPRODUCTS "${desktop_directory}/module.json"
    COMMAND "${CMAKE_COMMAND}" "-DLIBRARY=$<TARGET_FILE:eve_desktop>" "-DMODULE_ID=${desktop_module}"
        "-DPLUGIN_ID=${desktop_id}" "-DMODULE_VERSION=${PROJECT_VERSION}" -DLOADER=qt-ui -DUNLOAD_POLICY=resident
        -P "${desktop_manifest_writer}" VERBATIM)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    install(TARGETS eve_desktop LIBRARY DESTINATION system/desktop RUNTIME DESTINATION system/desktop)
    install(FILES "${desktop_directory}/module.json" "${desktop_directory}/messages.en.json" DESTINATION system/desktop)
endif()
