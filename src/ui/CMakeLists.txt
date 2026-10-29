include("${PROJECT_SOURCE_DIR}/cmake/Ela.cmake")
add_library(cordis_ui STATIC pages.cpp services.cpp management.cpp workbench.cpp home.cpp preferences.cpp brand.cpp
    "${PROJECT_SOURCE_DIR}/app/shell/resources/eve_brand.qrc")
set_target_properties(cordis_ui PROPERTIES AUTORCC ON)
add_library(Cordis::Ui ALIAS cordis_ui)
target_link_libraries(cordis_ui PUBLIC Cordis::Qt Qt5::Widgets PRIVATE Ela::WidgetTools)
cordis_target_defaults(cordis_ui)

add_library(cordis_config_ui STATIC configuration.cpp configuration_management.cpp)
add_library(Cordis::ConfigUi ALIAS cordis_config_ui)
target_link_libraries(cordis_config_ui PUBLIC Cordis::Ui Cordis::ConfigSession PRIVATE Cordis::ConfigIoQt Ela::WidgetTools)
cordis_target_defaults(cordis_config_ui)
