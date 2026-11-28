cmake_minimum_required(VERSION 3.21)
file(REAL_PATH "${HOST_BUILD}" host)
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef suffix)
set(scratch "${host}/ela-sdk-${suffix}")
if(EXISTS "${scratch}")
    message(FATAL_ERROR "Standalone build directory already exists: ${scratch}")
endif()
file(COPY "${DESKTOP_SOURCE}/" DESTINATION "${scratch}/source")
include("${STAGE_SDK_SCRIPT}")
stage_consumer_sdk("${scratch}/sdk")
file(COPY "${scratch}/sdk" DESTINATION "${scratch}/previous")
set(other "${scratch}/previous/sdk")
set(consumer_options -G "${HOST_GENERATOR}"
    "-DCMAKE_MAKE_PROGRAM=${HOST_MAKE_PROGRAM}" "-DCMAKE_CXX_COMPILER=${HOST_CXX_COMPILER}"
    "-DCMAKE_BUILD_TYPE=${HOST_CONFIG}" "-DQt5_DIR=${HOST_QT_DIR}" "-DCMAKE_PREFIX_PATH=${scratch}/sdk")
foreach(package IN ITEMS Eve ElaWidgetTools)
    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${CMAKE_CURRENT_LIST_DIR}/workbench_prefix"
        -B "${scratch}/prefix-${package}" ${consumer_options} "-DPRIOR_PACKAGE=${package}" "-DPRIOR_PREFIX=${other}"
        COMMAND_ERROR_IS_FATAL ANY)
endforeach()
# Both prefixes held valid packages for the import checks. Make stale cache resolution observable.
foreach(config IN ITEMS "${EVE_CMAKE_SUBDIR}/EveConfig.cmake" "ElaWidgetTools/lib/cmake/ElaWidgetToolsConfig.cmake")
    file(APPEND "${other}/${config}" "\nmessage(FATAL_ERROR \"Consumer used a stale package directory.\")\n")
endforeach()
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${scratch}/source" -B "${scratch}/build" ${consumer_options}
    -DCMAKE_CXX_STANDARD=20 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    "-DEVE_MSVC_SHOWINCLUDES_PREFIX=${HOST_INCLUDE_PREFIX}"
    "-DEve_DIR=${other}/${EVE_CMAKE_SUBDIR}" "-DElaWidgetTools_DIR=${other}/ElaWidgetTools/lib/cmake"
    COMMAND_ERROR_IS_FATAL ANY)
file(READ "${scratch}/build/compile_commands.json" commands)
if(commands MATCHES "[-/]std:c\\+\\+20" OR NOT commands MATCHES "[-/]std:c\\+\\+17")
    message(FATAL_ERROR "The desktop's compiler mode must match the C++17 module metadata.")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${scratch}/build" --target eve_desktop --parallel 2
    COMMAND_ERROR_IS_FATAL ANY)

# Deploy just the plugin, then start the product host from that installation.
set(bin "${scratch}/runtime")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${scratch}/build" --config "${HOST_CONFIG}" --prefix "${bin}"
    COMMAND_ERROR_IS_FATAL ANY)
set(catalog "${bin}/system/desktop/messages.en.json")
file(READ "${catalog}" messages)
string(JSON default_undo GET "${messages}" "ela.menu.undo")
if(default_undo STREQUAL "")
    message(FATAL_ERROR "The stand-alone installation is missing its default desktop messages.")
endif()
string(JSON messages SET "${messages}" "ela.menu.undo" "\"Undo from deployed catalog\"")
file(WRITE "${catalog}" "${messages}\n")
get_filename_component(exe "${DESKTOP_TEST_EXE}" NAME)
# The Windows module loader resolves private dependencies in the application directory.
file(COPY "${DESKTOP_TEST_EXE}" ${HOST_LIBRARIES} DESTINATION "${bin}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "EVE_TEST_EDITED_DESKTOP_CATALOG=1"
    "${bin}/${exe}" unused "${scratch}/workspace" recovered COMMAND_ERROR_IS_FATAL ANY)
file(REAL_PATH "${scratch}" actual_scratch)
if(NOT actual_scratch STREQUAL scratch)
    message(FATAL_ERROR "Standalone build directory moved: ${scratch}")
endif()
file(REMOVE_RECURSE "${scratch}")
