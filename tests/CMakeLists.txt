# Smoke test: the facade header and version surface.
add_executable(shrewd-lv-view-smoke test_smoke.c)

target_link_libraries(shrewd-lv-view-smoke
        PRIVATE
            shrewd-lv-view
            shrewd-lv-view-warnings)

add_test(NAME shrewd-lv-view.smoke COMMAND shrewd-lv-view-smoke)

# Dependency wiring: shrewd-basic and lvgl arrive as imported packages
# (find_package off the dependency prefix) - this proves the headers and
# the archives really resolve. It links shrewd-lv-view rather than the
# two packages directly, so the transitive usage requirements are what
# is under test.
add_executable(shrewd-lv-view-deps test_deps.c)

target_link_libraries(shrewd-lv-view-deps
        PRIVATE
            shrewd-lv-view
            shrewd-lv-view-warnings)

add_test(NAME shrewd-lv-view.deps COMMAND shrewd-lv-view-deps)

# 绑定机制的单测:无头 LVGL 夹具 + 机制断言(design.md §13)。
add_executable(shrewd-lv-view-bind
        test_bind.c
        lvgl_fixture.c)

target_link_libraries(shrewd-lv-view-bind
        PRIVATE
            shrewd-lv-view
            shrewd-lv-view-warnings)

add_test(NAME shrewd-lv-view.bind COMMAND shrewd-lv-view-bind)

# Test binaries return non-zero on failure, so a plain COMMAND run is enough.
