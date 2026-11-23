# shrewd-lvgl-vue: a Vue-style reactive architecture for LVGL in pure C.
# Build: cmake -S . -B build && cmake --build build && ctest --test-dir build
cmake_minimum_required(VERSION 3.20)
project(shrewd-lv-view
        VERSION 0.1.0
        DESCRIPTION "Vue-style reactive architecture for LVGL in pure C"
        LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(SHREWD_LV_VIEW_BUILD_TESTS "Build the tests" ${PROJECT_IS_TOP_LEVEL})
option(SHREWD_LV_VIEW_INSTALL "Generate install rules" ${PROJECT_IS_TOP_LEVEL})
option(SHREWD_LV_VIEW_BUILD_STATIC "Build the static library" ON)

# Warnings applied to our own targets only (link against this INTERFACE target).
add_library(shrewd-lv-view-warnings INTERFACE)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(shrewd-lv-view-warnings INTERFACE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
elseif(MSVC)
    target_compile_options(shrewd-lv-view-warnings INTERFACE /W4
            /D_CRT_SECURE_NO_WARNINGS)
endif()

enable_testing()

# The core library (reactive primitives; zero LVGL dependencies).
file(GLOB_RECURSE SHREWD_LV_VIEW_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/library/*.c")
if(SHREWD_LV_VIEW_BUILD_STATIC)
    add_library(shrewd-lv-view STATIC ${SHREWD_LV_VIEW_SOURCES})
    add_library(shrewd-lv-view::shrewd-lv-view ALIAS shrewd-lv-view)
    target_include_directories(shrewd-lv-view PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>)
    target_link_libraries(shrewd-lv-view PRIVATE shrewd-lv-view-warnings)
endif()

if(SHREWD_LV_VIEW_BUILD_TESTS)
    add_subdirectory(tests)
endif()
