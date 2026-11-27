add_library(eve_loader STATIC configuration.cpp)
add_library(Eve::Loader ALIAS eve_loader)
target_include_directories(eve_loader PUBLIC "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/sdk/include>")
eve_target_defaults(eve_loader)

add_subdirectory("${PROJECT_SOURCE_DIR}/third_party/libyaml" "${CMAKE_BINARY_DIR}/third_party/libyaml")
add_library(eve_config_yaml STATIC yaml.cpp validation.cpp)
add_library(Eve::ConfigYaml ALIAS eve_config_yaml)
target_link_libraries(eve_config_yaml PUBLIC Eve::Loader PRIVATE eve_libyaml)
eve_target_defaults(eve_config_yaml)

add_library(eve_config_coordinator STATIC coordinator.cpp)
add_library(Eve::ConfigCoordinator ALIAS eve_config_coordinator)
target_link_libraries(eve_config_coordinator PUBLIC Eve::ConfigYaml Eve::Core)
eve_target_defaults(eve_config_coordinator)

add_library(eve_config_definitions STATIC definitions.cpp)
add_library(Eve::ConfigDefinitions ALIAS eve_config_definitions)
target_link_libraries(eve_config_definitions PUBLIC Eve::ConfigYaml Eve::Core)
eve_target_defaults(eve_config_definitions)
target_link_libraries(eve_config_coordinator PUBLIC Eve::ConfigDefinitions)

add_library(eve_config_kernel STATIC kernel_driver.cpp replacement_claim.cpp)
add_library(Eve::ConfigKernel ALIAS eve_config_kernel)
target_link_libraries(eve_config_kernel PUBLIC Eve::ConfigCoordinator Eve::ConfigDefinitions)
eve_target_defaults(eve_config_kernel)

add_library(eve_config_store STATIC store.cpp)
add_library(Eve::ConfigStore ALIAS eve_config_store)
target_link_libraries(eve_config_store PUBLIC Eve::ConfigYaml Eve::Core)
eve_target_defaults(eve_config_store)

add_library(eve_config_session STATIC session.cpp target_commit.cpp)
add_library(Eve::ConfigSession ALIAS eve_config_session)
target_link_libraries(eve_config_session PUBLIC Eve::ConfigCoordinator Eve::ConfigStore Eve::ConfigDefinitions)
eve_target_defaults(eve_config_session)

add_library(eve_config_replacement STATIC
    configuration_commit.cpp
    configuration_reconstruction.cpp
)
add_library(Eve::ConfigReplacement ALIAS eve_config_replacement)
target_link_libraries(eve_config_replacement PUBLIC Eve::ConfigSession Eve::ConfigKernel)
eve_target_defaults(eve_config_replacement)

if(EVE_REQUIRE_QT5152)
    add_library(eve_config_file_qt STATIC qt_file.cpp)
    add_library(Eve::ConfigFileQt ALIAS eve_config_file_qt)
    target_link_libraries(eve_config_file_qt PUBLIC Eve::ConfigYaml Qt5::Core)
    target_compile_definitions(eve_config_file_qt PUBLIC QT_NO_KEYWORDS)
    eve_target_defaults(eve_config_file_qt)

    add_library(eve_config_io_qt STATIC qt_io.cpp qt_watcher.cpp)
    add_library(Eve::ConfigIoQt ALIAS eve_config_io_qt)
    target_link_libraries(eve_config_io_qt PUBLIC Eve::ConfigStore Eve::ConfigFileQt Eve::Qt)
    eve_target_defaults(eve_config_io_qt)

    add_library(eve_config_store_qt STATIC qt_store.cpp)
    add_library(Eve::ConfigStoreQt ALIAS eve_config_store_qt)
    target_link_libraries(eve_config_store_qt PUBLIC Eve::ConfigStore Eve::ConfigFileQt)
    eve_target_defaults(eve_config_store_qt)
endif()
