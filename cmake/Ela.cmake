# 使用仓库内维护的 Ela 基础库，只构建库子目录，不执行上游示例或覆盖 Qt 路径。
set(cordis_ela_SOURCE_DIR "${PROJECT_SOURCE_DIR}/third_party/ela")

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
