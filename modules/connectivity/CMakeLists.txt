# module: Connectivity Center. Probes configured targets, records samples and
# outages, notifies on outage transitions.

add_library(nexus_module_connectivity
    src/migrations.cpp
    src/connectivity_repository.cpp
    src/connectivity_report.cpp
    src/prober.cpp
    src/default_probe.cpp
    src/connectivity_module.cpp)
add_library(nexus::module_connectivity ALIAS nexus_module_connectivity)

target_sources(nexus_module_connectivity PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
        include/nexus/module/connectivity/connectivity_repository.hpp
        include/nexus/module/connectivity/connectivity_report.hpp
        include/nexus/module/connectivity/prober.hpp
        include/nexus/module/connectivity/connectivity_module.hpp)

target_include_directories(nexus_module_connectivity PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>")

target_compile_features(nexus_module_connectivity PUBLIC cxx_std_20)
target_link_libraries(nexus_module_connectivity
    PUBLIC
        nexus::core
        nexus::jobs
        nexus::app_services
    PRIVATE
        nexus::db
        nexus::net
        nexus::notify
        nexuspc::warnings)
