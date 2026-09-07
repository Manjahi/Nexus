cmake_minimum_required(VERSION 3.25)

project(NexusPC
    VERSION 0.1.0
    DESCRIPTION "Unified local-first computer management and protection suite"
    LANGUAGES CXX)

# --- Standards -------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "" FORCE)
endif()

# --- Options -------------------------------------------------------------
option(NEXUSPC_BUILD_TESTS   "Build unit/integration tests" ON)
option(NEXUSPC_BUILD_DESKTOP "Build the Qt desktop app"     ON)
option(NEXUSPC_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)

# Shared interface target carrying warning flags.
add_library(nexuspc_warnings INTERFACE)
add_library(nexuspc::warnings ALIAS nexuspc_warnings)
nexuspc_set_project_warnings(nexuspc_warnings ${NEXUSPC_WARNINGS_AS_ERRORS})

# --- Testing ------------------------------------------------------------
if(NEXUSPC_BUILD_TESTS)
    enable_testing()
    include(CTest)
endif()

# --- Components -------------------------------------------------------
add_subdirectory(libs/core)
add_subdirectory(libs/db)
add_subdirectory(libs/jobs)
add_subdirectory(libs/notify)

add_subdirectory(apps/agent)
add_subdirectory(apps/vault)

if(NEXUSPC_BUILD_DESKTOP)
    add_subdirectory(apps/desktop)
endif()

if(NEXUSPC_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# --- Summary ----------------------------------------------------------
message(STATUS "")
message(STATUS "NexusPC ${PROJECT_VERSION}")
message(STATUS "  Tests   : ${NEXUSPC_BUILD_TESTS}")
message(STATUS "  Desktop : ${NEXUSPC_BUILD_DESKTOP}")
message(STATUS "  Warnings as errors: ${NEXUSPC_WARNINGS_AS_ERRORS}")
message(STATUS "")
