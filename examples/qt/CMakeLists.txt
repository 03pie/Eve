add_executable(cordis_ela_demo ela_demo.cpp)
target_link_libraries(cordis_ela_demo PRIVATE Cordis::Ui)
set_target_properties(cordis_ela_demo PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
cordis_target_defaults(cordis_ela_demo)
