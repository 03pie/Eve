add_executable(cordis_sdk_contract unit/sdk_contract.cpp unit/header_isolation.cpp)
target_link_libraries(cordis_sdk_contract PRIVATE Cordis::Core)
cordis_target_defaults(cordis_sdk_contract)
add_test(NAME foundation.sdk_contract COMMAND cordis_sdk_contract)
set_tests_properties(foundation.sdk_contract PROPERTIES LABELS "foundation;sdk")

add_executable(cordis_event_tests unit/events.cpp)
target_link_libraries(cordis_event_tests PRIVATE Cordis::Core)
cordis_target_defaults(cordis_event_tests)
foreach(case IN ITEMS snapshot once_filter bail_observe next serial parallel lifetime epoch blocked validation snapshot_lifetime async_edges result_copy)
    add_test(NAME w6.events_${case} COMMAND cordis_event_tests ${case})
    set_tests_properties(w6.events_${case} PROPERTIES LABELS "w6;core" TIMEOUT 15)
endforeach()

if(TARGET cordis_event_demo)
    add_test(NAME w6.event_demo COMMAND cordis_event_demo)
    set_tests_properties(w6.event_demo PROPERTIES LABELS "w6;example" TIMEOUT 15)
endif()

add_executable(cordis_w1_tests unit/w1.cpp)
target_link_libraries(cordis_w1_tests PRIVATE Cordis::Core)
cordis_target_defaults(cordis_w1_tests)
foreach(case IN ITEMS
    result_values result_move_only scheduler_fifo scheduler_thread scheduler_limit scheduler_recovery
    task_ready_queued task_failure_queued task_move_only_observers task_completion_race
    task_registration_race task_closed_registration task_discard_queued task_closed_completion
    task_destroyed_scheduler task_callback_failure task_broken_promise task_receipt_chain
    task_invalid_observer cancellation_independent task_producer_copies task_throwing_scheduler
    scheduler_close_race scheduler_close_reentrant)
    add_test(NAME w1.${case} COMMAND cordis_w1_tests ${case})
    set_tests_properties(w1.${case} PROPERTIES LABELS "w1;core" TIMEOUT 15)
endforeach()

add_executable(cordis_children_tests unit/children.cpp)
target_link_libraries(cordis_children_tests PRIVATE Cordis::Core)
cordis_target_defaults(cordis_children_tests)
foreach(case IN ITEMS optional ordered cycle update_realms failure guards reentrant_stop declared_cycle declaration_boundaries pending_failure)
    add_test(NAME w5.children_${case} COMMAND cordis_children_tests ${case})
    set_tests_properties(w5.children_${case} PROPERTIES LABELS "w5;core" TIMEOUT 15)
endforeach()

if(TARGET cordis_effect_demo)
    add_test(NAME w2.effect_demo COMMAND cordis_effect_demo)
    set_tests_properties(w2.effect_demo PROPERTIES LABELS "w2;example" TIMEOUT 15)
endif()

if(TARGET cordis_task_demo)
    add_test(NAME w1.task_demo COMMAND cordis_task_demo)
    set_tests_properties(w1.task_demo PROPERTIES LABELS "w1;example" TIMEOUT 15)
endif()

add_executable(cordis_w2_tests unit/w2.cpp)
target_link_libraries(cordis_w2_tests PRIVATE Cordis::Core)
cordis_target_defaults(cordis_w2_tests)
foreach(case IN ITEMS
    owner_once dropped_token reverse_order reentrant_setup setup_failure async_setup_failure
    async_cleanup cleanup_errors cleanup_registration broadcast_cancel adopted_group adoption_cycle
    delivery_blocked wrong_thread clock_ordering clock_limits observation_timeout
    observation_completion observation_clock_closed observation_immediate_failure
    async_registration_guard stale_context blocked_setup_delivery retains_failed_closure blocked_cleanup_delivery cleanup_destructor_guard
    clock_step_budget clock_thread_reentry observation_delayed_failure observation_deadline_race
    timeout_barriers adopted_failure clock_budget_errors)
    add_test(NAME w2.${case} COMMAND cordis_w2_tests ${case})
    set_tests_properties(w2.${case} PROPERTIES LABELS "w2;core" TIMEOUT 15)
endforeach()

add_executable(cordis_w3_tests unit/w3.cpp)
target_link_libraries(cordis_w3_tests PRIVATE Cordis::Core)
cordis_target_defaults(cordis_w3_tests)
add_test(NAME w3.single_activation COMMAND cordis_w3_tests)
set_tests_properties(w3.single_activation PROPERTIES LABELS "w3;core" TIMEOUT 15)

add_executable(cordis_cleanup_context_tests unit/cleanup_context.cpp)
target_link_libraries(cordis_cleanup_context_tests PRIVATE Cordis::Core)
cordis_target_defaults(cordis_cleanup_context_tests)
foreach(case IN ITEMS epoch_window failures token_boundary)
    add_test(NAME w3.cleanup_${case} COMMAND cordis_cleanup_context_tests ${case})
    set_tests_properties(w3.cleanup_${case} PROPERTIES LABELS "w3;core" TIMEOUT 15)
endforeach()

add_executable(cordis_service_tests unit/service.cpp)
target_link_libraries(cordis_service_tests PRIVATE Cordis::Core)
cordis_target_defaults(cordis_service_tests)
foreach(case IN ITEMS lifecycle realms call_barrier threads_close ownership_reentry)
    add_test(NAME w4.service_${case} COMMAND cordis_service_tests ${case})
    set_tests_properties(w4.service_${case} PROPERTIES LABELS "w4;core" TIMEOUT 15)
endforeach()

add_executable(cordis_committed_service_tests unit/committed_service.cpp)
target_link_libraries(cordis_committed_service_tests PRIVATE Cordis::Core)
cordis_target_defaults(cordis_committed_service_tests)
foreach(case IN ITEMS retirement boundaries epochs_failure reentrant_call)
    add_test(NAME w4.committed_${case} COMMAND cordis_committed_service_tests ${case})
    set_tests_properties(w4.committed_${case} PROPERTIES LABELS "w4;core" TIMEOUT 15)
endforeach()

add_executable(cordis_dependency_tests unit/dependency.cpp)
target_link_libraries(cordis_dependency_tests PRIVATE Cordis::Core)
cordis_target_defaults(cordis_dependency_tests)
foreach(case IN ITEMS recovery transitive update_draining rollback cycle_realm blocked_cleanup pending_stop_type)
    add_test(NAME w4.dependency_${case} COMMAND cordis_dependency_tests ${case})
    set_tests_properties(w4.dependency_${case} PROPERTIES LABELS "w4;core" TIMEOUT 15)
endforeach()

add_executable(cordis_service_contract_tests unit/service_contract.cpp)
target_link_libraries(cordis_service_contract_tests PRIVATE Cordis::Core)
cordis_target_defaults(cordis_service_contract_tests)
foreach(case IN ITEMS required versions realms async async_blocked validation)
    add_test(NAME w4.contract_${case} COMMAND cordis_service_contract_tests ${case})
    set_tests_properties(w4.contract_${case} PROPERTIES LABELS "w4;core" TIMEOUT 15)
endforeach()
