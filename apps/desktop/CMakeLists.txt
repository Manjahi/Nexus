# nexuspc-ui : Qt desktop shell (Milestone 1 target).

find_package(Qt6 6.5 QUIET COMPONENTS Widgets)

if(NOT Qt6_FOUND)
    message(WARNING
        "Qt6 (Widgets) not found - skipping apps/desktop.\n"
        "  Point CMAKE_PREFIX_PATH at your Qt 6 install, or configure with the\n"
        "  'ci-windows' preset / -DNEXUSPC_BUILD_DESKTOP=OFF to build the backend only.")
    return()
endif()

qt_standard_project_setup()

qt_add_executable(nexuspc_desktop WIN32
    src/main.cpp
    src/Platform.cpp
    src/Platform.hpp
    src/NotificationBridge.cpp
    src/NotificationBridge.hpp
    src/MainWindow.cpp
    src/MainWindow.hpp)

target_link_libraries(nexuspc_desktop
    PRIVATE
        nexus::core
        nexus::db
        nexus::jobs
        nexus::notify
        nexus::app_services
        nexuspc::warnings
        Qt6::Widgets)

set_target_properties(nexuspc_desktop PROPERTIES
    OUTPUT_NAME "NexusPC"
    AUTOMOC ON)

if(NEXUSPC_BUILD_TESTS)
    add_test(NAME desktop_selftest COMMAND nexuspc_desktop --selftest)
    set_tests_properties(desktop_selftest PROPERTIES
        ENVIRONMENT "NEXUSPC_DB=${CMAKE_CURRENT_BINARY_DIR}/desktop_selftest.db")
endif()

# Stage the Qt runtime next to the executable so it runs from the build tree.
if(WIN32)
    get_target_property(_qt_bin Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qt_bin}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET nexuspc_desktop POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations --no-compiler-runtime
                    --$<IF:$<CONFIG:Debug>,debug,release> "$<TARGET_FILE:nexuspc_desktop>"
            COMMENT "Deploying Qt runtime for NexusPC"
            VERBATIM)
    endif()
endif()
