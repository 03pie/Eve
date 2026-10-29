# Qt-specific fixtures/entry point are independent of native ABI fixture setup.
foreach(case IN ITEMS good compiler qt runtime iterator pointer sdk missing catalog)
    set(toolchain "{\"sdkVersion\":\"${PROJECT_VERSION}\",\"qtVersion\":\"5.15.2\",\"compiler\":\"MSVC\",\"compilerVersion\":\"${CMAKE_CXX_COMPILER_VERSION}\",\"runtime\":\"$<IF:$<CONFIG:Debug>,MDd,MD>\",\"pointerBits\":64,\"cppStandard\":201703,\"iteratorDebug\":$<IF:$<CONFIG:Debug>,2,0>,\"debug\":$<IF:$<CONFIG:Debug>,true,false>}")
    if(case STREQUAL "compiler")
        string(REPLACE "\"MSVC\"" "\"Other\"" toolchain "${toolchain}")
    elseif(case STREQUAL "qt")
        string(REPLACE "5.15.2" "5.15.1" toolchain "${toolchain}")
    elseif(case STREQUAL "runtime")
        string(REPLACE "\"runtime\":\"$<IF:$<CONFIG:Debug>,MDd,MD>\"" "\"runtime\":\"MT\"" toolchain "${toolchain}")
    elseif(case STREQUAL "iterator")
        string(REPLACE "\"iteratorDebug\":$<IF:$<CONFIG:Debug>,2,0>" "\"iteratorDebug\":9" toolchain "${toolchain}")
    elseif(case STREQUAL "pointer")
        string(REPLACE "\"pointerBits\":64" "\"pointerBits\":32" toolchain "${toolchain}")
    elseif(case STREQUAL "sdk")
        string(REPLACE "${PROJECT_VERSION}" "9.0.0" toolchain "${toolchain}")
    elseif(case STREQUAL "missing")
        set(toolchain "{}")
    endif()
    set(plugins "[\"org.cordis.qt.page\",\"org.cordis.qt.settings\"]")
    if(case STREQUAL "catalog")
        set(plugins "[\"org.cordis.wrong\",\"org.cordis.qt.settings\"]")
    endif()
    set(directory "${CMAKE_CURRENT_BINARY_DIR}/qt-preflight/${case}/$<CONFIG>")
    file(GENERATE OUTPUT "${directory}/qt_metadata.json"
        CONTENT "{\"moduleId\":\"org.cordis.qt.example\",\"moduleVersion\":\"1.0.0\",\"plugins\":${plugins},\"toolchain\":${toolchain}}")
    # The embedded contract is a real input of moc, not only of the C++ compiler.
    # Track it directly so metadata edits cannot leave a valid-looking stale DLL.
    add_custom_command(OUTPUT "${directory}/qt_metadata.moc"
        COMMAND Qt5::moc "-I${directory}" "${PROJECT_SOURCE_DIR}/tests/fixtures/qt_metadata.cpp"
            -o "${directory}/qt_metadata.moc"
        DEPENDS "${PROJECT_SOURCE_DIR}/tests/fixtures/qt_metadata.cpp" "${directory}/qt_metadata.json" VERBATIM)
    add_library(cordis_qt_metadata_${case} MODULE "${PROJECT_SOURCE_DIR}/tests/fixtures/qt_metadata.cpp"
        "${directory}/qt_metadata.moc")
    set_target_properties(cordis_qt_metadata_${case} PROPERTIES PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/qt-metadata" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/qt-metadata")
    target_include_directories(cordis_qt_metadata_${case} PRIVATE "${directory}")
    target_link_libraries(cordis_qt_metadata_${case} PRIVATE Qt5::Core)
    cordis_target_defaults(cordis_qt_metadata_${case})
    add_dependencies(cordis_module_qt_preflight_tests cordis_qt_metadata_${case})
    add_test(NAME p4.qt_preflight_${case} COMMAND cordis_module_qt_preflight_tests "${case}" "$<TARGET_FILE:cordis_qt_metadata_${case}>")
    set_tests_properties(p4.qt_preflight_${case} PROPERTIES LABELS "p4;module;qt;preflight" TIMEOUT 15)
endforeach()
foreach(case IN ITEMS backend thread order)
    add_test(NAME p4.qt_preflight_${case} COMMAND cordis_module_qt_preflight_tests "${case}" "$<TARGET_FILE:cordis_qt_metadata_good>")
    set_tests_properties(p4.qt_preflight_${case} PROPERTIES LABELS "p4;module;qt;preflight" TIMEOUT 15)
endforeach()
add_dependencies(cordis_module_qt_preflight_tests cordis_fixture_good)
add_test(NAME p4.qt_preflight_not_qt COMMAND cordis_module_qt_preflight_tests not_qt "$<TARGET_FILE:cordis_fixture_good>")
set_tests_properties(p4.qt_preflight_not_qt PROPERTIES LABELS "p4;module;qt;preflight" TIMEOUT 15)
