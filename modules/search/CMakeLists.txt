# module: Local Search. Persisted postings + an in-memory BM25 index, file
# text extraction, and query with snippets.

add_library(nexus_module_search
    src/migrations.cpp
    src/content_reader.cpp
    src/search_repository.cpp
    src/search_indexer.cpp
    src/search_report.cpp
    src/search_module.cpp)
add_library(nexus::module_search ALIAS nexus_module_search)

target_sources(nexus_module_search PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
        include/nexus/module/search/content_reader.hpp
        include/nexus/module/search/search_repository.hpp
        include/nexus/module/search/search_indexer.hpp
        include/nexus/module/search/search_report.hpp
        include/nexus/module/search/search_module.hpp)

target_include_directories(nexus_module_search PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>")

target_compile_features(nexus_module_search PUBLIC cxx_std_20)
target_link_libraries(nexus_module_search
    PUBLIC
        nexus::core
        nexus::app_services
        nexus::fs
        nexus::search
    PRIVATE
        nexus::db
        nexuspc::warnings)
