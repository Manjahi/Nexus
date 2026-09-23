# libnexus-fs : reusable exclusion rules, a recursive file walker, and a
# recursive directory-change watcher.

add_library(nexus_fs
    src/exclusion_rules.cpp
    src/walker.cpp)
add_library(nexus::fs ALIAS nexus_fs)

target_sources(nexus_fs PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
        include/nexus/fs/exclusion_rules.hpp
        include/nexus/fs/walker.hpp
        include/nexus/fs/directory_watcher.hpp)

target_include_directories(nexus_fs PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>")

target_compile_features(nexus_fs PUBLIC cxx_std_20)

if(WIN32)
    target_sources(nexus_fs PRIVATE src/windows/directory_watcher_windows.cpp)
else()
    target_sources(nexus_fs PRIVATE src/stub_directory_watcher.cpp)
endif()

target_link_libraries(nexus_fs PRIVATE nexuspc::warnings)
