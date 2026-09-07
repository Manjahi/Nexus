# nexuspc-agent : optional background service (sampling, polling, schedules).
# Stub until Milestone 2 - see docs/adr/0002-process-model-and-platform-scope.md.

add_executable(nexuspc_agent src/main.cpp)
target_link_libraries(nexuspc_agent PRIVATE nexus::core nexuspc::warnings)
set_target_properties(nexuspc_agent PROPERTIES OUTPUT_NAME "nexuspc-agent")
