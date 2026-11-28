set(miniz_root "${PROJECT_SOURCE_DIR}/third_party/miniz")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/miniz_export.h" "#pragma once\n#define MINIZ_EXPORT\n")
add_library(eve_zip_reader STATIC "${miniz_root}/miniz.c" "${miniz_root}/miniz_tinfl.c" "${miniz_root}/miniz_zip.c")
target_include_directories(eve_zip_reader PUBLIC "${miniz_root}" "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(eve_zip_reader PUBLIC MINIZ_NO_STDIO MINIZ_NO_DEFLATE_APIS MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
add_library(eve_extensions STATIC package.cpp catalog.cpp reclaim.cpp qt_module.cpp)
add_library(Eve::Extensions ALIAS eve_extensions)
target_include_directories(eve_extensions PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/src/module" "${PROJECT_SOURCE_DIR}/contracts/workbench")
target_link_libraries(eve_extensions PUBLIC Eve::Core Qt5::Core PRIVATE Eve::ModuleInstall Eve::ModuleQtPreflight Eve::ModuleQt eve_zip_reader)
eve_target_defaults(eve_extensions)

if(BUILD_TESTING AND EVE_BUILD_ELA_UI AND EVE_SHARED_CORE)
    add_executable(eve_extension_catalog_tests tests/catalog_test.cpp)
    eve_target_defaults(eve_extension_catalog_tests)
    target_link_libraries(eve_extension_catalog_tests PRIVATE Eve::Extensions)
    add_dependencies(eve_extension_catalog_tests eve_example_notebook eve_example_notebook_a)
    add_test(NAME extensions.offline_catalog COMMAND eve_extension_catalog_tests
        "${CMAKE_COMMAND}" "${PROJECT_SOURCE_DIR}/cmake/WriteExtensionPackage.cmake"
        "$<TARGET_FILE:eve_example_notebook>" "$<TARGET_FILE:eve_example_notebook_a>")
endif()
