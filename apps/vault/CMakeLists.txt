# nexuspc-vault : isolated vault process (own threat model, built last).
# Stub until Milestone 7 - see docs/adr/0002-process-model-and-platform-scope.md.

add_executable(nexuspc_vault src/main.cpp)
target_link_libraries(nexuspc_vault PRIVATE nexus::core nexuspc::warnings)
set_target_properties(nexuspc_vault PROPERTIES OUTPUT_NAME "nexuspc-vault")
