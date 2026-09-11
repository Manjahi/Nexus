# nexuspc-vault : isolated vault process. See docs/adr/0003-vault-security-architecture.md.

add_subdirectory(lib)

add_executable(nexuspc_vault src/main.cpp)
target_link_libraries(nexuspc_vault PRIVATE nexus::core nexuspc::warnings)
set_target_properties(nexuspc_vault PROPERTIES OUTPUT_NAME "nexuspc-vault")
