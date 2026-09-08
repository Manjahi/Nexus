# module: System Health. Samples the machine into the Health tables and raises
# threshold notifications.

add_library(nexus_module_hardware
    src/migrations.cpp
    src/hardware_repository.cpp
    src/sampler.cpp
    src/hardware_module.cpp)
add_library(nexus::module_hardware ALIAS nexus_module_hardware)

target_sources(nexus_module_hardware PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
        include/nexus/module/hardware/hardware_repository.hpp
        include/nexus/module/hardware/sampler.hpp
        include/nexus/module/hardware/hardware_module.hpp)

target_include_directories(nexus_module_hardware PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>")

target_compile_features(nexus_module_hardware PUBLIC cxx_std_20)
target_link_libraries(nexus_module_hardware
    PUBLIC
        nexus::core
        nexus::jobs
        nexus::app_services
    PRIVATE
        nexus::db
        nexus::notify
        nexus::system
        nexuspc::warnings)
