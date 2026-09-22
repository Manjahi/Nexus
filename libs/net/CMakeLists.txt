# libnexus-net : latency/connectivity probes (DNS, ICMP, TCP connect) and the
# stats that summarise a run of them.

find_package(CURL CONFIG REQUIRED)

add_library(nexus_net
    src/probe.cpp
    src/latency_stats.cpp
    src/http_probe.cpp)
add_library(nexus::net ALIAS nexus_net)

target_sources(nexus_net PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
        include/nexus/net/probe.hpp
        include/nexus/net/latency_stats.hpp)

target_include_directories(nexus_net PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>")

target_compile_features(nexus_net PUBLIC cxx_std_20)

if(WIN32)
    target_sources(nexus_net PRIVATE src/windows/probe_windows.cpp src/windows/gateway_windows.cpp)
    target_link_libraries(nexus_net PRIVATE ws2_32 iphlpapi)
else()
    target_sources(nexus_net PRIVATE src/stub_probe.cpp)
endif()

target_link_libraries(nexus_net PRIVATE nexuspc::warnings CURL::libcurl)
