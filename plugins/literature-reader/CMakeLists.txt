cmake_minimum_required(VERSION 3.21)
project(LiteratureReader VERSION 1.1.0 LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "LiteratureReader currently requires Windows x64 and the matching MSVC SDK.")
endif()
find_package(Cordis 0.1.5 EXACT CONFIG REQUIRED COMPONENTS QtUiPluginSdk)
set(reader_windows_sdk_version "$ENV{WindowsSDKVersion}")
string(REGEX REPLACE "[/\\]+$" "" reader_windows_sdk_version "${reader_windows_sdk_version}")
find_path(READER_CPPWINRT_INCLUDE_DIR winrt/Windows.Data.Pdf.h
    HINTS "$ENV{WindowsSdkDir}/Include/${reader_windows_sdk_version}/cppwinrt")
if(NOT READER_CPPWINRT_INCLUDE_DIR)
    message(FATAL_ERROR "Windows C++/WinRT headers required; configure in the Windows SDK developer environment.")
endif()
cordis_add_qt_ui_plugin(literature_reader
    SOURCE plugin.cpp
    MODULE_ID org.cordis.literature-reader
    PLUGIN_ID literature.reader
    MODULE_VERSION ${PROJECT_VERSION}
    SOURCES reader_page.cpp reader_page.hpp reader_library.cpp reader_library.hpp pdf_renderer.cpp pdf_renderer.hpp)
target_compile_features(literature_reader PRIVATE cxx_std_17)
target_compile_options(literature_reader PRIVATE /utf-8 /W4)
target_compile_definitions(literature_reader PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_include_directories(literature_reader PRIVATE "${READER_CPPWINRT_INCLUDE_DIR}")
target_link_libraries(literature_reader PRIVATE WindowsApp)

include(CTest)
if(BUILD_TESTING)
    add_executable(reader_library_tests tests/reader_library_tests.cpp reader_library.cpp reader_library.hpp)
    target_compile_features(reader_library_tests PRIVATE cxx_std_17)
    target_compile_options(reader_library_tests PRIVATE /utf-8 /W4)
    target_include_directories(reader_library_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(reader_library_tests PRIVATE Qt5::Core)
    foreach(scene IN ITEMS import_reopen state_reopen conflict locked_retry bad_metadata import_failures
                           worker_preparation invalid_edits readonly_retry export_notes)
        add_test(NAME reader.data_${scene} COMMAND reader_library_tests ${scene})
    endforeach()
endif()
