# 可交互 demo:一个 SDL 窗口,鼠标当触摸。
#
# **框架不碰 SDL** —— 窗口/像素/事件都在 `src/platform/sdl_port.c`,那是接自己
# 板子时要写的那层。所以这里只是**又一个普通消费者**:find_package 拿框架和
# 组件库,和任何第三方应用一样。
#
#     cmake -S . -B build -DCMAKE_PREFIX_PATH=<prefix>
#     cmake --build build && ./build/demo
#     SDL_VIDEODRIVER=dummy ./build/demo --ticks 120     # 无头跑一截
cmake_minimum_required(VERSION 3.20)
project(shrewd-lv-view-demo LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(shrewd-lv-view CONFIG REQUIRED)
find_package(demo-components CONFIG REQUIRED)

# SDL2 只有这个 demo 要,框架不要 —— 所以走 pkg-config,不进框架的依赖链。
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

add_executable(demo
        src/main.c
        src/views.c
        src/devices.c
        src/platform/sdl_port.c)

target_include_directories(demo PRIVATE src ${SDL2_INCLUDE_DIRS})
target_link_libraries(demo PRIVATE
        shrewd-lv-view::shrewd-lv-view
        demo-components::demo-components
        ${SDL2_LIBRARIES})
target_compile_options(demo PRIVATE ${SDL2_CFLAGS_OTHER})
