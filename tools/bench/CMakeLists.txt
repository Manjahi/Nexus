# nexuspc-bench : manual throughput benchmark for storage scan / backup /
# search indexing against a synthetic dataset (Milestone 8 performance pass,
# docs/PERFORMANCE.md). Not part of the test suite.

add_executable(nexuspc_bench src/main.cpp)
target_link_libraries(nexuspc_bench
    PRIVATE
        nexus::core
        nexus::db
        nexus::fs
        nexus::module_storage
        nexus::module_backup
        nexus::module_search
        nexuspc::warnings)
set_target_properties(nexuspc_bench PROPERTIES OUTPUT_NAME "nexuspc-bench")
