# 固定上游源码，只构建静态库，不执行上游测试、安装或包导出配置。
add_library(cordis_libyaml STATIC
    src/api.c src/dumper.c src/emitter.c src/loader.c
    src/parser.c src/reader.c src/scanner.c src/writer.c
)
target_include_directories(cordis_libyaml PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(cordis_libyaml PUBLIC YAML_DECLARE_STATIC PRIVATE
    YAML_VERSION_MAJOR=0 YAML_VERSION_MINOR=2 YAML_VERSION_PATCH=5 YAML_VERSION_STRING="0.2.5"
)
if(MSVC)
    target_compile_definitions(cordis_libyaml PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
configure_file(LICENSE "${CMAKE_BINARY_DIR}/licenses/LibYAML.LICENSE" COPYONLY)
