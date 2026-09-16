add_subdirectory(unit)
add_subdirectory(integration)
# add_subdirectory(system)  # not populated - desktop_selftest (apps/desktop)
# already serves as the system-level smoke test: full platform bring-up,
# a real job run, a real notification round-trip, and real report
# generation, run headless through ctest. Revisit only if a scenario needs
# more than that (e.g. driving the actual GUI in CI).
