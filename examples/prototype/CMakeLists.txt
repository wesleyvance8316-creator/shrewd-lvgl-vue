# 真实页面原型:倒推 API 手感(设计文档 design.md §12 里程碑 3 的第 7 项)
cmake_minimum_required(VERSION 3.20)
project(shrewd-lv-view-prototype LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(shrewd-lv-view CONFIG REQUIRED)
# 组件库是**独立的包**,跟框架一样靠 find_package 导入(design.md §7.7)。
find_package(demo-components CONFIG REQUIRED)

add_executable(prototype
        src/main.c
        src/feed.c
        src/stores/device.c
        src/views/views.c
        src/components/device_card/device_card.c)

target_include_directories(prototype PRIVATE src)
target_link_libraries(prototype PRIVATE
        shrewd-lv-view::shrewd-lv-view
        demo-components::demo-components)
