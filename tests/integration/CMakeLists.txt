# Integration tests: real modules wired to a real Database/ThreadPool/
# Scheduler through ModuleHost - see test_platform_bringup.cpp's header
# comment for what this covers that the per-module unit tests don't.

find_package(Catch2 CONFIG REQUIRED)

add_executable(nexus_integration_tests
    test_platform_bringup.cpp
    test_backup_schedule_restart.cpp
    test_storage_search_hook.cpp)

target_link_libraries(nexus_integration_tests
    PRIVATE
        nexus::app_services
        nexus::db
        nexus::jobs
        nexus::notify
        nexus::module_hardware
        nexus::module_connectivity
        nexus::module_backup
        nexus::module_storage
        nexuspc::warnings
        Catch2::Catch2WithMain)

add_test(NAME nexus_integration_tests COMMAND nexus_integration_tests)
