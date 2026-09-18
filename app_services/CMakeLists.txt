# application-services : the layer that composes the libraries into one platform
# (module registry, event bus, audit trail, and later job/notification
# persistence, alerts, reports, permissions).

add_library(nexus_app_services
    src/audit_log.cpp
    src/module_registry.cpp
    src/module_host.cpp
    src/job_repository.cpp
    src/notification_repository.cpp
    src/report_center.cpp
    src/heavy_job_guard.cpp
    src/retention_setting.cpp)
add_library(nexus::app_services ALIAS nexus_app_services)

target_sources(nexus_app_services PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
        include/nexus/services/event_bus.hpp
        include/nexus/services/audit_log.hpp
        include/nexus/services/module_registry.hpp
        include/nexus/services/module.hpp
        include/nexus/services/module_host.hpp
        include/nexus/services/job_repository.hpp
        include/nexus/services/notification_repository.hpp
        include/nexus/services/report_center.hpp
        include/nexus/services/report_format.hpp
        include/nexus/services/heavy_job_guard.hpp
        include/nexus/services/service_context.hpp
        include/nexus/services/retention_setting.hpp)

target_include_directories(nexus_app_services PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>")

target_compile_features(nexus_app_services PUBLIC cxx_std_20)
target_link_libraries(nexus_app_services
    PUBLIC
        nexus::core
        nexus::db
        nexus::jobs
        nexus::notify
    PRIVATE
        nexuspc::warnings)
