# nexus_vault_core : vault file format, entry model, and the unlock-session
# store. Linked only by apps/vault (the process) and its own tests - nothing
# outside apps/vault may depend on this target (see ADR-0003: the vault is a
# security boundary, not a module).

find_package(nlohmann_json CONFIG REQUIRED)

add_library(nexus_vault_core
    src/entry.cpp
    src/vault_file.cpp
    src/vault_store.cpp)
add_library(nexus::vault_core ALIAS nexus_vault_core)

target_sources(nexus_vault_core PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
        include/nexus/vault/entry.hpp
        include/nexus/vault/vault_file.hpp
        include/nexus/vault/vault_store.hpp)

target_include_directories(nexus_vault_core PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>")

target_compile_features(nexus_vault_core PUBLIC cxx_std_20)
target_link_libraries(nexus_vault_core
    PUBLIC
        nexus::core
        nexus::crypto
    PRIVATE
        nlohmann_json::nlohmann_json
        nexuspc::warnings)
