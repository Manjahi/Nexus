# libnexus-ipc : local IPC transport (Windows named pipes), first used by the
# vault split (ADR-0003). Windows-only, matching ADR-0002's platform scope.

add_library(nexus_ipc src/pipe.cpp)
add_library(nexus::ipc ALIAS nexus_ipc)

target_sources(nexus_ipc PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES include/nexus/ipc/pipe.hpp)

target_include_directories(nexus_ipc PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>")

target_compile_features(nexus_ipc PUBLIC cxx_std_20)
target_link_libraries(nexus_ipc PRIVATE nexuspc::warnings)

if(WIN32)
    target_link_libraries(nexus_ipc PRIVATE advapi32)
endif()
