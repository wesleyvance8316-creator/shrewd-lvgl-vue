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

# 组件模型的单测:demo_card 就是样板本身(design.md §7.2)。
add_executable(shrewd-lv-view-component
        test_component.c
        demo_card.c
        lvgl_fixture.c)

target_link_libraries(shrewd-lv-view-component
        PRIVATE
            shrewd-lv-view
            shrewd-lv-view-warnings)

add_test(NAME shrewd-lv-view.component COMMAND shrewd-lv-view-component)

# 导航栈的单测:四阶段、释放策略、pop_until、上限、守卫(design.md §8)。
add_executable(shrewd-lv-view-nav
        test_nav.c
        lvgl_fixture.c)

target_link_libraries(shrewd-lv-view-nav
        PRIVATE
            shrewd-lv-view
            shrewd-lv-view-warnings)

add_test(NAME shrewd-lv-view.nav COMMAND shrewd-lv-view-nav)

# 栈恢复的单测:快照往返、未知页面、坏快照(design.md §8.6)。
add_executable(shrewd-lv-view-restore
        test_restore.c
        lvgl_fixture.c)

target_link_libraries(shrewd-lv-view-restore
        PRIVATE
            shrewd-lv-view
            shrewd-lv-view-warnings)

add_test(NAME shrewd-lv-view.restore COMMAND shrewd-lv-view-restore)

# 分支与模态栈的单测(architecture.md §6.9)。
add_executable(shrewd-lv-view-branch
        test_branch.c
        lvgl_fixture.c)

target_link_libraries(shrewd-lv-view-branch
        PRIVATE
            shrewd-lv-view
            shrewd-lv-view-warnings)

add_test(NAME shrewd-lv-view.branch COMMAND shrewd-lv-view-branch)

# Test binaries return non-zero on failure, so a plain COMMAND run is enough.
