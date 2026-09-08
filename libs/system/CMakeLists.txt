# libnexus-system : live machine metrics behind a platform-neutral provider
# interface. Windows provider first; Linux/macOS providers land later.

add_library(nexus_system)
add_library(nexus::system ALIAS nexus_system)

target_sources(nexus_system PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
        include/nexus/system/metrics.hpp
        include/nexus/system/system_provider.hpp)

target_include_directories(nexus_system PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>")

target_compile_features(nexus_system PUBLIC cxx_std_20)

if(WIN32)
    target_sources(nexus_system PRIVATE src/windows/windows_system_provider.cpp)
    target_link_libraries(nexus_system PRIVATE ntdll iphlpapi psapi)
else()
    target_sources(nexus_system PRIVATE src/stub_system_provider.cpp)
endif()

target_link_libraries(nexus_system PRIVATE nexuspc::warnings)
