# libnexus-hash : BLAKE3 and SHA-256 over buffers and files.

find_package(blake3 CONFIG REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)

add_library(nexus_hash src/hash.cpp)
add_library(nexus::hash ALIAS nexus_hash)

target_sources(nexus_hash PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES include/nexus/hash/hash.hpp)

target_include_directories(nexus_hash PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>")

target_compile_features(nexus_hash PUBLIC cxx_std_20)
target_link_libraries(nexus_hash
    PRIVATE
        BLAKE3::blake3
        unofficial-sodium::sodium
        nexuspc::warnings)
