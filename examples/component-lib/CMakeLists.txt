# demo-components —— 一个**独立的组件库**(docs/design.md §7.7)。
#
# 它就是**普通 C 库**:include 自己的头,打成 CMake 包,靠 find_package 导入 ——
# 与 shrewd-lv-view 被消费的方式完全一致。**命名前缀用自己的 `demo_`,不占用
# `shrewd_`**(§7.7):组件库是第三方,不该假装是框架的一部分。
#
# 建法(与依赖同处一个 prefix):
#     cmake -S . -B build -DCMAKE_PREFIX_PATH=<deps+framework prefix>
#     cmake --build build && cmake --install build --prefix <同一个 prefix>
cmake_minimum_required(VERSION 3.20)
project(demo-components VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(DEMO_COMPONENTS_BUILD_TESTS "Build the tests" ON)

add_library(demo-components-warnings INTERFACE)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(demo-components-warnings INTERFACE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

# 组件库依赖框架 —— 与应用消费框架的方式一模一样(find_package,不是 add_subdirectory)。
find_package(shrewd-lv-view CONFIG REQUIRED)

add_library(demo-components STATIC
        library/card.c
        library/gauge.c)
add_library(demo-components::demo-components ALIAS demo-components)

target_include_directories(demo-components PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
# PUBLIC:公开头暴露 lv_obj_t / shrewd_basic_reflect_ref_t,消费者编译时两个都要在场。
# warnings 用 BUILD_INTERFACE 包住:静态库会把私有链接依赖记进导出集,
# 而这个是我们自己的,不该跟着包走(shrewd-lv-view 同样处理)。
target_link_libraries(demo-components
        PUBLIC shrewd-lv-view::shrewd-lv-view
        PRIVATE $<BUILD_INTERFACE:demo-components-warnings>)

# ---------------------------------------------------------------------------
# 包导出:与 shrewd-lv-view 自己被消费的方式一致
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS demo-components
        EXPORT demo-components-targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/demo"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT demo-components-targets
        FILE demo-components-targets.cmake
        NAMESPACE demo-components::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/demo-components)
configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/demo-components-config.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/demo-components-config.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/demo-components)
write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/demo-components-config-version.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)
install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/demo-components-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/demo-components-config-version.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/demo-components)

if(DEMO_COMPONENTS_BUILD_TESTS)
    enable_testing()
    add_executable(demo-components-test-card tests/test_card.c tests/lvgl_fixture.c)
    target_link_libraries(demo-components-test-card
            PRIVATE demo-components demo-components-warnings)
    add_test(NAME demo-components.card COMMAND demo-components-test-card)

    add_executable(demo-components-test-gauge tests/test_gauge.c tests/lvgl_fixture.c)
    target_link_libraries(demo-components-test-gauge
            PRIVATE demo-components demo-components-warnings)
    add_test(NAME demo-components.gauge COMMAND demo-components-test-gauge)
endif()
