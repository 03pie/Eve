# 使用固定归档构建库子目录，避免上游顶层覆盖 Qt 路径或构建其示例。
include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
FetchContent_Declare(cordis_ela
    URL https://codeload.github.com/Liniyous/ElaWidgetTools/zip/aae58f5d0d98e05eabe10c263aa7da5f2491d702
    URL_HASH SHA256=3f52ac2a47387ebaee0975c72a251795873f927cffee6ba179b17f2d1a2a307a
    DOWNLOAD_DIR "${PROJECT_SOURCE_DIR}/build/deps"
    DOWNLOAD_NAME ela-aae58f5.zip
    SOURCE_SUBDIR cordis-do-not-use-upstream-root
)
FetchContent_MakeAvailable(cordis_ela)

# 此固定版本的导出宏仅支持 DLL；第三方选项限定在函数作用域。
function(cordis_build_ela)
    set(QT_VERSION_MAJOR 5)
    set(QT_VERSION 5.15.2)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)
    set(ELAWIDGETTOOLS_BUILD_STATIC_LIB OFF)
    add_subdirectory("${cordis_ela_SOURCE_DIR}/ElaWidgetTools" "${CMAKE_BINARY_DIR}/third_party/ela")
    set_target_properties(ElaWidgetTools PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(MSVC)
        target_compile_options(ElaWidgetTools PRIVATE /utf-8)
    endif()
endfunction()
cordis_build_ela()
configure_file("${cordis_ela_SOURCE_DIR}/LICENSE" "${CMAKE_BINARY_DIR}/bin/ElaWidgetTools.LICENSE" COPYONLY)
