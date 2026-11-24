include_guard(GLOBAL)
option(CORDIS_INSTALL_SDK "Install the matching shared Windows Qt plugin SDK" OFF)
if(NOT CORDIS_INSTALL_SDK)
    return()
endif()

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8
   OR NOT CORDIS_SHARED_CORE OR NOT TARGET cordis_qt_ui_plugin_sdk)
    message(FATAL_ERROR "Cordis SDK installation requires Windows/MSVC/x64, CORDIS_SHARED_CORE, Qt 5.15.2 and Ela UI")
endif()
if(CMAKE_CONFIGURATION_TYPES OR NOT CMAKE_BUILD_TYPE MATCHES "^(Debug|Release)$")
    message(FATAL_ERROR "Cordis SDK installation requires a single Debug or Release configuration")
endif()
set(cordis_sdk_runtime "MultiThreadedDLL")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(cordis_sdk_runtime "MultiThreadedDebugDLL")
endif()
if(DEFINED CMAKE_MSVC_RUNTIME_LIBRARY AND NOT CMAKE_MSVC_RUNTIME_LIBRARY STREQUAL "${cordis_sdk_runtime}"
   AND NOT CMAKE_MSVC_RUNTIME_LIBRARY STREQUAL "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    message(FATAL_ERROR "Cordis SDK installation requires the matching dynamic MSVC CRT (/MD or /MDd)")
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
set(cordis_sdk_cmake_dir "${CMAKE_INSTALL_LIBDIR}/cmake/Cordis")
target_include_directories(cordis_core PUBLIC "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
foreach(pair IN ITEMS "cordis_core,Core" "cordis_qt,Qt" "cordis_ui,Ui"
                      "cordis_qt_plugin_sdk,QtPluginSdk" "cordis_qt_ui_plugin_sdk,QtUiPluginSdk")
    string(REPLACE "," ";" fields "${pair}")
    list(GET fields 0 target)
    list(GET fields 1 exported_name)
    set_target_properties(${target} PROPERTIES EXPORT_NAME "${exported_name}")
endforeach()
install(TARGETS cordis_core cordis_qt cordis_ui cordis_qt_plugin_sdk cordis_qt_ui_plugin_sdk
    EXPORT CordisTargets RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}" ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(EXPORT CordisTargets FILE CordisTargets.cmake NAMESPACE Cordis:: DESTINATION "${cordis_sdk_cmake_dir}")
install(DIRECTORY "${PROJECT_SOURCE_DIR}/sdk/include/" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
configure_package_config_file("${CMAKE_CURRENT_LIST_DIR}/CordisConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/CordisConfig.cmake" INSTALL_DESTINATION "${cordis_sdk_cmake_dir}")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/CordisConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}" COMPATIBILITY ExactVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/CordisConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/CordisConfigVersion.cmake"
    "${CMAKE_CURRENT_LIST_DIR}/CordisPlugin.cmake" "${CMAKE_CURRENT_LIST_DIR}/QtPluginContract.cmake"
    "${CMAKE_CURRENT_LIST_DIR}/WriteModuleManifest.cmake" DESTINATION "${cordis_sdk_cmake_dir}")
install(DIRECTORY "${PROJECT_SOURCE_DIR}/sdk/templates/qt-ui-plugin" DESTINATION "${CMAKE_INSTALL_DATADIR}/Cordis/templates")
install(FILES "${PROJECT_SOURCE_DIR}/docs/sdk-consumer.md" DESTINATION "${CMAKE_INSTALL_DATADIR}/Cordis")
