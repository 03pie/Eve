include("${PROJECT_SOURCE_DIR}/cmake/ModuleExceptionModel.cmake")
eve_module_exception_model()
add_library(eve_module STATIC ledger.cpp)
add_library(Eve::Module ALIAS eve_module)
target_link_libraries(eve_module PUBLIC Eve::Core)
eve_target_defaults(eve_module)

add_library(eve_module_abi STATIC abi.cpp)
add_library(Eve::ModuleAbi ALIAS eve_module_abi)
target_link_libraries(eve_module_abi PUBLIC Eve::Module)
eve_target_defaults(eve_module_abi)

add_library(eve_module_requests STATIC request.cpp json.cpp state_wire.cpp)
add_library(Eve::ModuleRequests ALIAS eve_module_requests)
target_link_libraries(eve_module_requests PUBLIC Eve::ModuleAbi)
eve_target_defaults(eve_module_requests)

if(WIN32 AND EVE_REQUIRE_QT5152)
    add_library(eve_module_preflight STATIC manifest.cpp artifact.cpp)
    add_library(Eve::ModulePreflight ALIAS eve_module_preflight)
    target_link_libraries(eve_module_preflight PUBLIC Eve::Module PRIVATE Qt5::Core)
    target_compile_definitions(eve_module_preflight PRIVATE QT_NO_KEYWORDS)
    eve_target_defaults(eve_module_preflight)

    add_library(eve_module_install STATIC install.cpp install_directory.cpp)
    add_library(Eve::ModuleInstall ALIAS eve_module_install)
    target_link_libraries(eve_module_install PUBLIC Eve::ModulePreflight PRIVATE Qt5::Core)
    target_compile_definitions(eve_module_install PRIVATE QT_NO_KEYWORDS)
    eve_target_defaults(eve_module_install)

    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        add_library(eve_module_qt_preflight STATIC qt_manifest.cpp)
        add_library(Eve::ModuleQtPreflight ALIAS eve_module_qt_preflight)
        target_link_libraries(eve_module_qt_preflight PUBLIC Eve::ModulePreflight PRIVATE Qt5::Core)
        target_compile_definitions(eve_module_qt_preflight PRIVATE QT_NO_KEYWORDS
            EVE_SDK_VERSION="${EVE_SDK_VERSION}" EVE_QT_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}")
        eve_target_defaults(eve_module_qt_preflight)
    endif()

    add_library(eve_module_native STATIC
        manager.cpp
        unload.cpp
        catalog.cpp
        catalog_registry.cpp
        reservation.cpp
        replacement_borrow.cpp
        replacement_sources.cpp
        replacement_commit.cpp
        runtime_origin.cpp
        instance_origin.cpp
    )
    add_library(Eve::ModuleNative ALIAS eve_module_native)
    target_link_libraries(eve_module_native PUBLIC Eve::ModulePreflight PRIVATE Qt5::Core)
    target_compile_definitions(eve_module_native PRIVATE QT_NO_KEYWORDS)
    eve_target_defaults(eve_module_native)

    add_library(eve_module_host STATIC host.cpp host_resources.cpp host_services.cpp runtime.cpp native_state.cpp)
    add_library(Eve::ModuleHost ALIAS eve_module_host)
    target_link_libraries(eve_module_host PUBLIC Eve::Module PRIVATE Eve::ModuleNative Eve::ModuleRequests Qt5::Core)
    target_compile_definitions(eve_module_host PRIVATE QT_NO_KEYWORDS)
    eve_target_defaults(eve_module_host)
endif()

if(TARGET eve_module_qt_preflight AND EVE_SHARED_CORE)
    add_library(eve_qt_plugin_sdk INTERFACE)
    add_library(Eve::QtPluginSdk ALIAS eve_qt_plugin_sdk)
    target_link_libraries(eve_qt_plugin_sdk INTERFACE Eve::Core Qt5::Core)

    add_library(eve_module_qt STATIC qt_module.cpp qt_runtime.cpp qt_state.cpp)
    add_library(Eve::ModuleQt ALIAS eve_module_qt)
    target_link_libraries(eve_module_qt PUBLIC Eve::ModuleNative PRIVATE Eve::ModuleQtPreflight Eve::ModuleRequests Eve::Qt Qt5::Core)
    target_compile_definitions(eve_module_qt PRIVATE QT_NO_KEYWORDS)
    eve_target_defaults(eve_module_qt)
endif()
