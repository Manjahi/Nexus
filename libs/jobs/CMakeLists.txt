# libnexus-jobs : thread pool, scheduler, cancellation, progress, retry/backoff.
# Deliberately free of a database dependency; job persistence lives in the
# application-services layer.

find_package(Threads REQUIRED)

add_library(nexus_jobs
    src/cancellation.cpp
    src/progress.cpp
    src/thread_pool.cpp
    src/retry.cpp
    src/schedule_table.cpp
    src/scheduler.cpp)
add_library(nexus::jobs ALIAS nexus_jobs)

target_sources(nexus_jobs PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
        include/nexus/jobs/cancellation.hpp
        include/nexus/jobs/progress.hpp
        include/nexus/jobs/thread_pool.hpp
        include/nexus/jobs/retry.hpp
        include/nexus/jobs/schedule_table.hpp
        include/nexus/jobs/scheduler.hpp)

target_include_directories(nexus_jobs PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>")

target_compile_features(nexus_jobs PUBLIC cxx_std_20)
target_link_libraries(nexus_jobs
    PUBLIC
        Threads::Threads
    PRIVATE
        nexuspc::warnings)
