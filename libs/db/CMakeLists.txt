# libnexus-db : SQLite connection/statement/transaction RAII, migration runner,
# repositories over the shared suite metadata.

find_package(unofficial-sqlite3 CONFIG REQUIRED)

add_library(nexus_db
    src/error.cpp
    src/database.cpp
    src/statement.cpp
    src/transaction.cpp
    src/migration.cpp
    src/migrations/core_schema.cpp
    src/settings_repository.cpp)
add_library(nexus::db ALIAS nexus_db)

target_sources(nexus_db PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
        include/nexus/db/error.hpp
        include/nexus/db/database.hpp
        include/nexus/db/statement.hpp
        include/nexus/db/transaction.hpp
        include/nexus/db/migration.hpp
        include/nexus/db/settings_repository.hpp)

target_include_directories(nexus_db PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>")

target_compile_features(nexus_db PUBLIC cxx_std_20)
target_link_libraries(nexus_db
    PRIVATE
        nexus::core
        nexuspc::warnings
        unofficial::sqlite3::sqlite3)
