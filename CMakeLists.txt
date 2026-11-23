# shrewd-lvgl-vue: a Vue-style reactive architecture for LVGL in pure C.
#
#   tools/fetch-deps.sh && tools/build-deps.sh
#   cmake -S . -B build && cmake --build build && ctest --test-dir build
cmake_minimum_required(VERSION 3.20)
project(shrewd-lv-view
        VERSION 0.1.0
        DESCRIPTION "Vue-style reactive architecture for LVGL in pure C"
        LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Built as its own project, or as a member of the shrewd workspace (the
# aggregate CMakeLists.txt one directory up). Everything that belongs to
# a standalone build - tests, install rules, package export - stays out
# of a parent project's way. Computed by hand rather than with
# PROJECT_IS_TOP_LEVEL, which is CMake 3.21 and this project claims 3.20.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(SHREWD_LV_VIEW_IS_TOP_LEVEL ON)
else()
    set(SHREWD_LV_VIEW_IS_TOP_LEVEL OFF)
endif()

option(SHREWD_LV_VIEW_BUILD_TESTS "Build the tests" ${SHREWD_LV_VIEW_IS_TOP_LEVEL})
option(SHREWD_LV_VIEW_INSTALL "Generate install rules" ${SHREWD_LV_VIEW_IS_TOP_LEVEL})
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

# ---------------------------------------------------------------------------
# External dependencies
# ---------------------------------------------------------------------------
# Third-party architecture: every external library is built as its own
# project (tools/build-deps.sh) and imported here as an installed CMake
# package - never add_subdirectory, never FetchContent into the build
# tree. They all install into one prefix, so one CMAKE_PREFIX_PATH entry
# resolves the whole set.
set(SHREWD_LV_VIEW_DEPS_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/build/prefix"
        CACHE PATH "Install prefix of the dependencies from tools/build-deps.sh")
if(EXISTS "${SHREWD_LV_VIEW_DEPS_PREFIX}")
    list(PREPEND CMAKE_PREFIX_PATH "${SHREWD_LV_VIEW_DEPS_PREFIX}")
endif()

# shrewd_lv_view_require_package(<name> [version] [CONFIG])
# find_package with an actionable failure: an unreadable CMake stack trace
# is a poor way to learn that the dependencies have not been built yet.
function(shrewd_lv_view_require_package name)
    find_package(${name} ${ARGN} QUIET)
    if(NOT ${name}_FOUND)
        message(FATAL_ERROR
                "The dependency '${name}' was not found.\n"
                "Build the dependencies first:\n"
                "    tools/fetch-deps.sh && tools/build-deps.sh\n"
                "or point -DSHREWD_LV_VIEW_DEPS_PREFIX= at an install prefix\n"
                "that already contains it.")
    endif()
endfunction()

# lvgl is external and released, so its version is part of the contract.
# shrewd-basic is pinned by deps/versions.lock instead: it is pre-1.0 and
# its minor number is not a compatibility claim worth enforcing here.
#
# Each lookup is skipped when its target already exists. Inside the shrewd
# workspace shrewd-basic is a sibling subproject, so its in-tree target is
# what gets linked - and "go to definition" then lands in its real source
# rather than in the installed copy under build/prefix. Standalone, the
# find_package path is unchanged. The two cannot both be present: a
# subproject's ALIAS and the imported target share a name.
if(NOT TARGET lvgl::lvgl)
    shrewd_lv_view_require_package(lvgl 9.6 CONFIG)
endif()
if(NOT TARGET shrewd-basic::shrewd-basic)
    shrewd_lv_view_require_package(shrewd-basic CONFIG)
endif()

# ---------------------------------------------------------------------------
# The library
# ---------------------------------------------------------------------------
# The binding layer: reactive core from shrewd-basic driving LVGL
# widgets (docs/initiation.md).
file(GLOB_RECURSE SHREWD_LV_VIEW_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/library/*.c")
if(SHREWD_LV_VIEW_BUILD_STATIC)
    include(GNUInstallDirs)
    add_library(shrewd-lv-view STATIC ${SHREWD_LV_VIEW_SOURCES})
    add_library(shrewd-lv-view::shrewd-lv-view ALIAS shrewd-lv-view)
    target_include_directories(shrewd-lv-view PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    # Both are PUBLIC: the public header exposes lv_obj_t* binding
    # targets and shrewd_basic_reflect_* types, so consumers compile
    # against both whether or not they touch them directly.
    #
    # The warnings target is wrapped in $<BUILD_INTERFACE:...>: a static
    # library still records private link dependencies in its export set,
    # and this one is ours alone - it must not travel with the package.
    target_link_libraries(shrewd-lv-view
            PUBLIC
                shrewd-basic::shrewd-basic
                lvgl::lvgl
            PRIVATE
                $<BUILD_INTERFACE:shrewd-lv-view-warnings>)
endif()

# ---------------------------------------------------------------------------
# Package export
# ---------------------------------------------------------------------------
# shrewd-lv-view ships the same way it consumes its own dependencies: an
# installed CMake package that yields shrewd-lv-view::shrewd-lv-view.
if(SHREWD_LV_VIEW_BUILD_STATIC AND SHREWD_LV_VIEW_INSTALL)
    include(CMakePackageConfigHelpers)

    install(TARGETS shrewd-lv-view
            EXPORT shrewd-lv-view-targets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/shrewd-lv-view"
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            PATTERN "*.in" EXCLUDE)

    install(EXPORT shrewd-lv-view-targets
            FILE shrewd-lv-view-targets.cmake
            NAMESPACE shrewd-lv-view::
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/shrewd-lv-view)
    configure_package_config_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/shrewd-lv-view-config.cmake.in"
            "${CMAKE_CURRENT_BINARY_DIR}/shrewd-lv-view-config.cmake"
            INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/shrewd-lv-view)
    write_basic_package_version_file(
            "${CMAKE_CURRENT_BINARY_DIR}/shrewd-lv-view-config-version.cmake"
            VERSION ${PROJECT_VERSION}
            COMPATIBILITY SameMajorVersion)
    install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/shrewd-lv-view-config.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/shrewd-lv-view-config-version.cmake"
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/shrewd-lv-view)
endif()

if(SHREWD_LV_VIEW_BUILD_TESTS)
    add_subdirectory(tests)
endif()
