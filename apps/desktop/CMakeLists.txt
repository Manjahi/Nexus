# nexuspc-ui : Qt desktop shell.

find_package(Qt6 6.5 QUIET COMPONENTS Widgets Charts)

if(NOT Qt6_FOUND)
    message(WARNING
        "Qt6 (Widgets, Charts) not found - skipping apps/desktop.\n"
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
    src/ChartWidget.cpp
    src/ChartWidget.hpp
    src/MainWindow.cpp
    src/MainWindow.hpp)

target_link_libraries(nexuspc_desktop
    PRIVATE
        nexus::core
        nexus::db
        nexus::jobs
        nexus::notify
        nexus::app_services
        nexus::module_hardware
        nexus::module_connectivity
        nexus::module_storage
        nexus::module_backup
        nexus::module_search
        nexus::module_network_center
        nexuspc::warnings
        Qt6::Widgets
        Qt6::Charts)

set_target_properties(nexuspc_desktop PROPERTIES
    OUTPUT_NAME "NexusPC"
    AUTOMOC ON)

if(NEXUSPC_BUILD_TESTS)
    set(_selftest_db "${CMAKE_CURRENT_BINARY_DIR}/desktop_selftest.db")

    # Start each run from a clean database so a schema change can't wedge it.
    add_test(NAME desktop_selftest_clean
        COMMAND ${CMAKE_COMMAND} -E rm -f
                "${_selftest_db}" "${_selftest_db}-wal" "${_selftest_db}-shm")
    set_tests_properties(desktop_selftest_clean PROPERTIES FIXTURES_SETUP selftest_db)

    add_test(NAME desktop_selftest COMMAND nexuspc_desktop --selftest)
    set_tests_properties(desktop_selftest PROPERTIES
        FIXTURES_REQUIRED selftest_db
        ENVIRONMENT "NEXUSPC_DB=${_selftest_db}")
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
