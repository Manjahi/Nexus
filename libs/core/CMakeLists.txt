# libnexus-core : IDs, time, errors, (later) settings / serialization / logging.

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/nexus/core/version.hpp"
    @ONLY)

add_library(nexus_core
    src/id.cpp
    src/time.cpp)
add_library(nexus::core ALIAS nexus_core)

target_sources(nexus_core PUBLIC
    FILE_SET HEADERS
    BASE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    FILES
        include/nexus/core/result.hpp
        include/nexus/core/id.hpp
        include/nexus/core/time.hpp
        "${CMAKE_CURRENT_BINARY_DIR}/generated/nexus/core/version.hpp")

target_include_directories(nexus_core PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>"
    "$<INSTALL_INTERFACE:include>")

target_compile_features(nexus_core PUBLIC cxx_std_20)
target_link_libraries(nexus_core PRIVATE nexuspc::warnings)
