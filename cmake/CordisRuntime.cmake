include_guard(GLOBAL)
option(CORDIS_INSTALL_RUNTIME "Enable the Windows desktop runtime installation component" OFF)
if(NOT CORDIS_INSTALL_RUNTIME)
    return()
endif()

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64"
   OR NOT TARGET cordis_plugin_workbench OR NOT CORDIS_SHARED_CORE
   OR CMAKE_CONFIGURATION_TYPES OR NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR "CordisRuntime requires the Windows x64 shared Qt/Ela Release preset.")
endif()

# SDK 和运行包可以共用构建；常规 SDK 安装只执行自己的安装规则。
set(runtime_component COMPONENT CordisRuntime EXCLUDE_FROM_ALL)
foreach(target IN ITEMS cordis_plugin_workbench cordis_log_query cordis_core cordis_qt cordis_ui
                        ElaWidgetTools Qt5::Core Qt5::Gui Qt5::Widgets)
    install(FILES "$<TARGET_FILE:${target}>" DESTINATION "." ${runtime_component})
endforeach()

get_target_property(qmake Qt5::qmake IMPORTED_LOCATION)
execute_process(COMMAND "${qmake}" -query QT_INSTALL_PLUGINS OUTPUT_VARIABLE qt_plugins
    OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
foreach(plugin IN ITEMS platforms/qwindows styles/qwindowsvistastyle imageformats/qgif
                        imageformats/qico imageformats/qjpeg imageformats/qwebp)
    get_filename_component(group "${plugin}" DIRECTORY)
    install(FILES "${qt_plugins}/${plugin}.dll" DESTINATION "${group}" ${runtime_component})
endforeach()
get_filename_component(qt_bin "${qmake}" DIRECTORY)
install(FILES "${qt_bin}/libEGL.dll" "${qt_bin}/libGLESv2.dll" DESTINATION "." ${runtime_component})
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/runtime/qt.conf"
    CONTENT "[Paths]\nPrefix=.\nPlugins=.\n")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/runtime/qt.conf" DESTINATION "." ${runtime_component})

# vcvars64 提供与编译器配套的正式重分发目录，Windows 10/11 自带 UCRT。
set(CORDIS_VC_REDIST_DIR "$ENV{VCToolsRedistDir}" CACHE PATH "Visual C++ redistributable root for the selected toolset")
set(vc_crt "${CORDIS_VC_REDIST_DIR}/x64/Microsoft.VC${MSVC_TOOLSET_VERSION}.CRT")
foreach(dll IN ITEMS msvcp140 msvcp140_1 vcruntime140 vcruntime140_1)
    if(NOT EXISTS "${vc_crt}/${dll}.dll")
        message(FATAL_ERROR "Missing ${dll}.dll in '${vc_crt}'. Set CORDIS_VC_REDIST_DIR to the compiler's redistributable root.")
    endif()
    install(FILES "${vc_crt}/${dll}.dll" DESTINATION "." ${runtime_component})
endforeach()

set(notices "${PROJECT_SOURCE_DIR}/packaging/windows/licenses")
install(FILES "${notices}/Runtime-Notices.txt" "${notices}/Qt-5.15.2.txt" DESTINATION licenses ${runtime_component})
foreach(component IN ITEMS ela libyaml)
    install(FILES "${PROJECT_SOURCE_DIR}/third_party/${component}/LICENSE"
        DESTINATION "licenses/${component}" ${runtime_component})
endforeach()
install(FILES "${PROJECT_SOURCE_DIR}/third_party/ela/RESOURCE-PROVENANCE.txt"
    "${PROJECT_SOURCE_DIR}/third_party/ela/UPSTREAM-RESOURCES.qrc.txt"
    DESTINATION licenses/ela ${runtime_component})
