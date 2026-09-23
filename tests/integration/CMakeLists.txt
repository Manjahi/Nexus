# Integration tests: real modules wired to a real Database/ThreadPool/
# Scheduler through ModuleHost - see test_platform_bringup.cpp's header
# comment for what this covers that the per-module unit tests don't.

find_package(Catch2 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(nexus_integration_tests
    test_platform_bringup.cpp
    test_backup_schedule_restart.cpp
    test_storage_search_hook.cpp
    test_vault_process.cpp)

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
        nexus::ipc
        nlohmann_json::nlohmann_json
        nexuspc::warnings
        Catch2::Catch2WithMain)

# test_vault_process.cpp locates nexuspc-vault.exe relative to its own
# build output path at runtime, but still needs the target to actually
# exist by the time it runs.
add_dependencies(nexus_integration_tests nexuspc_vault)

add_test(NAME nexus_integration_tests COMMAND nexus_integration_tests)
