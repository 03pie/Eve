add_executable(eve_foundation main.cpp)
target_link_libraries(eve_foundation PRIVATE Eve::Core)
eve_target_defaults(eve_foundation)

add_executable(eve_task_demo task_demo.cpp)
target_link_libraries(eve_task_demo PRIVATE Eve::Core)
eve_target_defaults(eve_task_demo)

add_executable(eve_effect_demo effect_demo.cpp)
target_link_libraries(eve_effect_demo PRIVATE Eve::Core)
eve_target_defaults(eve_effect_demo)

add_executable(eve_event_demo event_demo.cpp)
target_link_libraries(eve_event_demo PRIVATE Eve::Core)
eve_target_defaults(eve_event_demo)

add_executable(eve_platform_demo platform_demo.cpp)
target_link_libraries(eve_platform_demo PRIVATE Eve::Core)
eve_target_defaults(eve_platform_demo)
add_executable(eve_configuration_demo configuration_demo.cpp)
target_link_libraries(eve_configuration_demo PRIVATE Eve::ConfigYaml)
eve_target_defaults(eve_configuration_demo)

add_executable(eve_reconcile_demo reconcile_demo.cpp)
target_link_libraries(eve_reconcile_demo PRIVATE Eve::ConfigKernel)
eve_target_defaults(eve_reconcile_demo)

add_executable(eve_child_epoch_demo child_epoch_demo.cpp)
target_link_libraries(eve_child_epoch_demo PRIVATE Eve::Core)
eve_target_defaults(eve_child_epoch_demo)
