# Smoke test: the facade header and version surface.
add_executable(shrewd-lv-view-smoke test_smoke.c)

target_link_libraries(shrewd-lv-view-smoke
        PRIVATE
            shrewd-lv-view
            shrewd-lv-view-warnings)

add_test(NAME shrewd-lv-view.smoke COMMAND shrewd-lv-view-smoke)

# Test binaries return non-zero on failure, so a plain COMMAND run is enough.
