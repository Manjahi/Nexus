# module: Backup & Recovery. Content-addressed store + snapshot manifests;
# full/incremental via store dedup, verify, and restore.

add_library(nexus_module_backup
    src/migrations.cpp
    src/object_store.cpp
    src/backup_repository.cpp
    src/backup_engine.cpp
    src/restore_engine.cpp
    src/backup_module.cpp)
add_library(nexus::module_backup ALIAS nexus_module_backup)

target_sources(nexus_module_backup PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
        include/nexus/module/backup/object_store.hpp
        include/nexus/module/backup/backup_repository.hpp
        include/nexus/module/backup/backup_engine.hpp
        include/nexus/module/backup/restore_engine.hpp
        include/nexus/module/backup/backup_module.hpp)

target_include_directories(nexus_module_backup PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>")

target_compile_features(nexus_module_backup PUBLIC cxx_std_20)
target_link_libraries(nexus_module_backup
    PUBLIC
        nexus::core
        nexus::app_services
        nexus::fs
        nexus::hash
        nexus::jobs
    PRIVATE
        nexus::db
        nexus::notify
        nexuspc::warnings)
