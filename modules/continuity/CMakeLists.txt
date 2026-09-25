# module: Continuity. Tracks disaster-recovery readiness (backup coverage,
# verified snapshots, Vault exports) and runs real recovery rehearsals.

add_library(nexus_module_continuity
    src/migrations.cpp
    src/continuity_repository.cpp
    src/continuity_readiness.cpp
    src/continuity_scenarios.cpp
    src/continuity_module.cpp)
add_library(nexus::module_continuity ALIAS nexus_module_continuity)

target_sources(nexus_module_continuity PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
        include/nexus/module/continuity/continuity_repository.hpp
        include/nexus/module/continuity/continuity_readiness.hpp
        include/nexus/module/continuity/continuity_scenarios.hpp
        include/nexus/module/continuity/continuity_module.hpp)

target_include_directories(nexus_module_continuity PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>")

target_compile_features(nexus_module_continuity PUBLIC cxx_std_20)
target_link_libraries(nexus_module_continuity
    PUBLIC
        nexus::core
        nexus::app_services
    PRIVATE
        nexus::db
        # Readiness scoring reads Backup's schema directly (coverage/
        # verification per tracked asset) - the same pull-based cross-module
        # read BackupModule::is_path_backed_up() already does in reverse.
        nexus::module_backup
        nexuspc::warnings)
