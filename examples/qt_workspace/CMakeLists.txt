# A production plugin has its own embedded toolchain contract and post-link manifest.
set(directory "${CMAKE_CURRENT_BINARY_DIR}/plugin/$<CONFIG>")
set(toolchain "{\"sdkVersion\":\"${PROJECT_VERSION}\",\"qtVersion\":\"5.15.2\",\"compiler\":\"MSVC\",\"compilerVersion\":\"${CMAKE_CXX_COMPILER_VERSION}\",\"runtime\":\"$<IF:$<CONFIG:Debug>,MDd,MD>\",\"pointerBits\":64,\"cppStandard\":201703,\"iteratorDebug\":$<IF:$<CONFIG:Debug>,2,0>,\"debug\":$<IF:$<CONFIG:Debug>,true,false>}")
file(GENERATE OUTPUT "${directory}/notebook_metadata.json"
    CONTENT "{\"moduleId\":\"org.cordis.example.notebook\",\"moduleVersion\":\"1.0.0\",\"plugins\":[\"example.notebook\"],\"toolchain\":${toolchain}}")
set(moc_includes "-I${PROJECT_SOURCE_DIR}/sdk/include" "-I${directory}")
foreach(include IN LISTS Qt5Core_INCLUDE_DIRS)
    list(APPEND moc_includes "-I${include}")
endforeach()
add_custom_command(OUTPUT "${directory}/notebook.moc"
    COMMAND Qt5::moc ${moc_includes} "${CMAKE_CURRENT_SOURCE_DIR}/notebook.cpp" -o "${directory}/notebook.moc"
    DEPENDS notebook.cpp "${directory}/notebook_metadata.json" "${PROJECT_SOURCE_DIR}/sdk/include/cordis/module/qt_plugin.hpp" VERBATIM)
add_library(cordis_example_notebook MODULE notebook.cpp "${directory}/notebook.moc")
target_include_directories(cordis_example_notebook PRIVATE "${directory}")
target_link_libraries(cordis_example_notebook PRIVATE Cordis::QtUiPluginSdk)
cordis_target_defaults(cordis_example_notebook)
set_target_properties(cordis_example_notebook PROPERTIES PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/qt-workspace/notebook"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/qt-workspace/notebook")
set_property(TARGET cordis_example_notebook APPEND PROPERTY LINK_DEPENDS "${PROJECT_SOURCE_DIR}/cmake/WriteModuleManifest.cmake")
add_custom_command(TARGET cordis_example_notebook POST_BUILD
    BYPRODUCTS "${CMAKE_BINARY_DIR}/bin/qt-workspace/notebook/module.json"
    COMMAND "${CMAKE_COMMAND}" "-DLIBRARY=$<TARGET_FILE:cordis_example_notebook>" "-DMODULE_ID=org.cordis.example.notebook"
        "-DPLUGIN_ID=example.notebook" -DLOADER=qt-ui -P "${PROJECT_SOURCE_DIR}/cmake/WriteModuleManifest.cmake" VERBATIM)
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/bin/qt-workspace")
configure_file(workspace.yaml "${CMAKE_BINARY_DIR}/bin/qt-workspace/workspace.yaml" COPYONLY)
add_library(cordis_notebook_host STATIC workspace.cpp diagnostics.cpp)
target_link_libraries(cordis_notebook_host PUBLIC Cordis::ModuleQt Cordis::ConfigUi Cordis::ConfigKernel Cordis::ConfigStoreQt)
target_link_libraries(cordis_notebook_host PRIVATE Ela::WidgetTools)
target_include_directories(cordis_notebook_host PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
cordis_target_defaults(cordis_notebook_host)
add_dependencies(cordis_notebook_host cordis_example_notebook)
if(CORDIS_BUILD_EXAMPLES)
    add_executable(cordis_qt_workspace_demo main.cpp)
    target_link_libraries(cordis_qt_workspace_demo PRIVATE cordis_notebook_host)
    cordis_target_defaults(cordis_qt_workspace_demo)
    cordis_attach_eve_icon(cordis_qt_workspace_demo)
endif()
if(BUILD_TESTING)
    add_executable(cordis_qt_workspace_tests "${PROJECT_SOURCE_DIR}/tests/qt/module_qt_workspace.cpp")
    target_link_libraries(cordis_qt_workspace_tests PRIVATE cordis_notebook_host)
    cordis_target_defaults(cordis_qt_workspace_tests)
    foreach(case IN ITEMS normal multi update toggle add save start_failure cleanup_failure diagnostics guards close_conflict output_limit)
        add_test(NAME p4.qt_workspace_${case} COMMAND cordis_qt_workspace_tests "${CMAKE_BINARY_DIR}/bin/qt-workspace" "${case}")
        set_tests_properties(p4.qt_workspace_${case} PROPERTIES LABELS "p4;module;qt;ui;integration" TIMEOUT 25
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
endif()
