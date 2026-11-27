include_guard(GLOBAL)
option(EVE_INSTALL_SDK "Install the matching shared Windows Qt plugin SDK" OFF)
if(NOT EVE_INSTALL_SDK)
    return()
endif()

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8
   OR NOT EVE_SHARED_CORE OR NOT TARGET eve_qt_ui_plugin_sdk)
    message(FATAL_ERROR "Eve SDK installation requires Windows/MSVC/x64, EVE_SHARED_CORE, Qt 5.15.2 and Ela UI")
endif()
if(CMAKE_CONFIGURATION_TYPES OR NOT CMAKE_BUILD_TYPE MATCHES "^(Debug|Release)$")
    message(FATAL_ERROR "Eve SDK installation requires a single Debug or Release configuration")
endif()
set(eve_sdk_runtime "MultiThreadedDLL")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(eve_sdk_runtime "MultiThreadedDebugDLL")
endif()
if(DEFINED CMAKE_MSVC_RUNTIME_LIBRARY AND NOT CMAKE_MSVC_RUNTIME_LIBRARY STREQUAL "${eve_sdk_runtime}"
   AND NOT CMAKE_MSVC_RUNTIME_LIBRARY STREQUAL "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    message(FATAL_ERROR "Eve SDK installation requires the matching dynamic MSVC CRT (/MD or /MDd)")
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
set(eve_sdk_cmake_dir "${CMAKE_INSTALL_LIBDIR}/cmake/Eve")
target_include_directories(eve_core PUBLIC "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
foreach(pair IN ITEMS "eve_core,Core" "eve_qt,Qt" "eve_ui,Ui"
                      "eve_qt_plugin_sdk,QtPluginSdk" "eve_qt_ui_plugin_sdk,QtUiPluginSdk")
    string(REPLACE "," ";" fields "${pair}")
    list(GET fields 0 target)
    list(GET fields 1 exported_name)
    set_target_properties(${target} PROPERTIES EXPORT_NAME "${exported_name}")
endforeach()
install(TARGETS eve_core eve_qt eve_ui eve_qt_plugin_sdk eve_qt_ui_plugin_sdk
    EXPORT EveTargets RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}" ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(EXPORT EveTargets FILE EveTargets.cmake NAMESPACE Eve:: DESTINATION "${eve_sdk_cmake_dir}")
install(DIRECTORY "${PROJECT_SOURCE_DIR}/sdk/include/" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
configure_package_config_file("${CMAKE_CURRENT_LIST_DIR}/EveConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/EveConfig.cmake" INSTALL_DESTINATION "${eve_sdk_cmake_dir}")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/EveConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}" COMPATIBILITY ExactVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/EveConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/EveConfigVersion.cmake"
    "${CMAKE_CURRENT_LIST_DIR}/EvePlugin.cmake" "${CMAKE_CURRENT_LIST_DIR}/QtPluginContract.cmake"
    "${CMAKE_CURRENT_LIST_DIR}/ModuleIdentity.cmake"
    "${CMAKE_CURRENT_LIST_DIR}/WriteModuleManifest.cmake" DESTINATION "${eve_sdk_cmake_dir}")
set(eve_sdk_share "${CMAKE_INSTALL_DATADIR}/Eve")
set(eve_template "${PROJECT_SOURCE_DIR}/sdk/templates/qt-ui-plugin")
configure_file("${eve_template}/CMakeLists.txt.in" "sdk-template/CMakeLists.txt" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sdk-template/CMakeLists.txt" "${eve_template}/plugin.cpp"
    "${eve_template}/README.md" DESTINATION "${eve_sdk_share}/templates/qt-ui-plugin")

# 延续原有指南入口，同时让指南与声明中的相对链接在安装前缀内可以离线阅读。
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/sdk-consumer.md" CONTENT
    "# Eve Qt 插件 SDK\n\n[外部插件开发指南](docs/sdk-consumer.md) · [第三方声明](THIRD_PARTY_NOTICES.md)\n")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sdk-consumer.md" "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
    DESTINATION "${eve_sdk_share}")
install(FILES "${PROJECT_SOURCE_DIR}/docs/sdk-consumer.md" DESTINATION "${eve_sdk_share}/docs")
foreach(component IN ITEMS ela libyaml)
    install(FILES "${PROJECT_SOURCE_DIR}/third_party/${component}/LICENSE" "${PROJECT_SOURCE_DIR}/third_party/${component}/UPSTREAM.md"
        DESTINATION "${eve_sdk_share}/third_party/${component}")
endforeach()
install(FILES "${PROJECT_SOURCE_DIR}/third_party/ela/RESOURCE-PROVENANCE.txt" "${PROJECT_SOURCE_DIR}/third_party/ela/UPSTREAM-RESOURCES.qrc.txt"
    DESTINATION "${eve_sdk_share}/third_party/ela")
install(FILES "${PROJECT_SOURCE_DIR}/app/shell/resources/README.md" "${PROJECT_SOURCE_DIR}/app/shell/resources/provenance.json"
    DESTINATION "${eve_sdk_share}/app/shell/resources")
