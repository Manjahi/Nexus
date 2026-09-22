# module: Network Center. Authorized-CIDR device discovery, periodic
# availability monitoring, and history/alerts on device transitions.

add_library(nexus_module_network_center
    src/cidr.cpp
    src/migrations.cpp
    src/network_repository.cpp
    src/network_report.cpp
    src/network_scanner.cpp
    src/device_monitor.cpp
    src/default_ping.cpp
    src/network_center_module.cpp)
add_library(nexus::module_network_center ALIAS nexus_module_network_center)

target_sources(nexus_module_network_center PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
        include/nexus/module/network_center/cidr.hpp
        include/nexus/module/network_center/network_repository.hpp
        include/nexus/module/network_center/network_report.hpp
        include/nexus/module/network_center/network_scanner.hpp
        include/nexus/module/network_center/device_monitor.hpp
        include/nexus/module/network_center/network_center_module.hpp)

target_include_directories(nexus_module_network_center PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>")

target_compile_features(nexus_module_network_center PUBLIC cxx_std_20)
target_link_libraries(nexus_module_network_center
    PUBLIC
        nexus::core
        nexus::jobs
        nexus::app_services
        # network_scanner.hpp exposes nexus::net::PingResult in its public
        # API (ScanPingFn/default_ping), so this needs to be PUBLIC too.
        nexus::net
    PRIVATE
        nexus::db
        nexus::notify
        nexuspc::warnings)
