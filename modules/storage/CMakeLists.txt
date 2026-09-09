# module: Storage Intelligence. Size-group -> partial hash -> full hash
# duplicate detection, persisted to the Storage tables.

add_library(nexus_module_storage
    src/migrations.cpp
    src/storage_repository.cpp
    src/duplicate_scanner.cpp
    src/recycle.cpp
    src/storage_report.cpp
    src/storage_module.cpp)
add_library(nexus::module_storage ALIAS nexus_module_storage)

target_sources(nexus_module_storage PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
        include/nexus/module/storage/duplicate_scanner.hpp
        include/nexus/module/storage/storage_repository.hpp
        include/nexus/module/storage/recycle.hpp
        include/nexus/module/storage/storage_report.hpp
        include/nexus/module/storage/storage_module.hpp)

target_include_directories(nexus_module_storage PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>")

target_compile_features(nexus_module_storage PUBLIC cxx_std_20)
target_link_libraries(nexus_module_storage
    PUBLIC
        nexus::core
        nexus::app_services
        nexus::fs
    PRIVATE
        nexus::db
        nexus::hash
        nexuspc::warnings)

if(WIN32)
    target_link_libraries(nexus_module_storage PRIVATE ole32 shell32)
endif()
