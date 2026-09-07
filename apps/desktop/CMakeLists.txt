# nexuspc-ui : Qt desktop shell (Milestone 1 target).

find_package(Qt6 6.5 QUIET COMPONENTS Widgets)

if(NOT Qt6_FOUND)
    message(WARNING
        "Qt6 (Widgets) not found - skipping apps/desktop.\n"
        "  Point CMAKE_PREFIX_PATH at your Qt 6 install, or configure with the\n"
        "  'windows-no-deps' preset / -DNEXUSPC_BUILD_DESKTOP=OFF to build core + tests only.")
    return()
endif()

qt_standard_project_setup()

qt_add_executable(nexuspc_desktop WIN32
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.hpp)

target_link_libraries(nexuspc_desktop
    PRIVATE
        nexus::core
        nexuspc::warnings
        Qt6::Widgets)

set_target_properties(nexuspc_desktop PROPERTIES
    OUTPUT_NAME "NexusPC"
    AUTOMOC ON)
