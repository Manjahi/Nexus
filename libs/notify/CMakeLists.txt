# libnexus-notify : in-process notification center. Desktop toast / tray
# surfacing is a platform concern layered on top later.

add_library(nexus_notify
    src/severity.cpp
    src/notification_center.cpp)
add_library(nexus::notify ALIAS nexus_notify)

target_sources(nexus_notify PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
        include/nexus/notify/severity.hpp
        include/nexus/notify/notification.hpp
        include/nexus/notify/notification_center.hpp)

target_include_directories(nexus_notify PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>")

target_compile_features(nexus_notify PUBLIC cxx_std_20)
target_link_libraries(nexus_notify
    PUBLIC
        nexus::core
    PRIVATE
        nexuspc::warnings)
