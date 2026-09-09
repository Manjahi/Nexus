# libnexus-search : tokenizer, in-memory inverted index (BM25), snippets.

add_library(nexus_search
    src/tokenizer.cpp
    src/inverted_index.cpp
    src/snippet.cpp)
add_library(nexus::search ALIAS nexus_search)

target_sources(nexus_search PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
        include/nexus/search/tokenizer.hpp
        include/nexus/search/inverted_index.hpp
        include/nexus/search/snippet.hpp)

target_include_directories(nexus_search PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>")

target_compile_features(nexus_search PUBLIC cxx_std_20)
target_link_libraries(nexus_search PRIVATE nexuspc::warnings)
