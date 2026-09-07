find_package(Catch2 3 REQUIRED)

add_executable(nexus_core_tests
    core/test_result.cpp
    core/test_id.cpp
    core/test_time.cpp
    core/test_version.cpp)

target_link_libraries(nexus_core_tests
    PRIVATE
        nexus::core
        nexuspc::warnings
        Catch2::Catch2WithMain)

add_executable(nexus_db_tests
    db/test_database.cpp
    db/test_transaction.cpp
    db/test_migration.cpp
    db/test_settings_repository.cpp)

target_link_libraries(nexus_db_tests
    PRIVATE
        nexus::db
        nexuspc::warnings
        Catch2::Catch2WithMain)

add_executable(nexus_jobs_tests
    jobs/test_cancellation.cpp
    jobs/test_progress.cpp
    jobs/test_thread_pool.cpp
    jobs/test_retry.cpp
    jobs/test_schedule_table.cpp
    jobs/test_scheduler.cpp)

target_link_libraries(nexus_jobs_tests
    PRIVATE
        nexus::jobs
        nexuspc::warnings
        Catch2::Catch2WithMain)

add_executable(nexus_notify_tests
    notify/test_severity.cpp
    notify/test_notification_center.cpp)

target_link_libraries(nexus_notify_tests
    PRIVATE
        nexus::notify
        nexuspc::warnings
        Catch2::Catch2WithMain)

add_executable(nexus_services_tests
    services/test_event_bus.cpp
    services/test_audit_log.cpp
    services/test_module_registry.cpp
    services/test_job_repository.cpp
    services/test_notification_repository.cpp)

target_link_libraries(nexus_services_tests
    PRIVATE
        nexus::app_services
        nexus::db
        nexuspc::warnings
        Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(nexus_core_tests)
catch_discover_tests(nexus_db_tests)
catch_discover_tests(nexus_jobs_tests)
catch_discover_tests(nexus_notify_tests)
catch_discover_tests(nexus_services_tests)
