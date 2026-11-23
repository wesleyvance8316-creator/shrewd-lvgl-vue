#!/bin/sh
# Build the dependency libraries in deps/ and install them into one prefix
# (tools/build-deps.sh).
#
# Each library is configured, built and installed as its own CMake project;
# nothing is add_subdirectory'd into anything and nothing lands in the
# consumer's build tree. The install prefix is what joins them:
#
#   deps/shrewd-basic -> $PREFIX/lib/cmake/shrewd-basic/
#   deps/lvgl         -> $PREFIX/lib/cmake/lvgl/
#
# shrewd-lvgl-vue then resolves both with find_package(<name> CONFIG) off
# that single prefix. Sources come from tools/fetch-deps.sh.
#
# Usage: tools/build-deps.sh [options]
#   --prefix DIR       install prefix (default: <root>/build/prefix)
#   --build-type TYPE  CMAKE_BUILD_TYPE (default: Release)
#   --clean            drop the dependency build trees and the prefix first
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(dirname "$here")
prefix="$root/build/prefix"
build_type=Release
clean=0

while [ $# -gt 0 ]; do
    case "$1" in
        --prefix)
            prefix=$2
            shift 2
            ;;
        --build-type)
            build_type=$2
            shift 2
            ;;
        --clean)
            clean=1
            shift
            ;;
        -h | --help)
            echo "usage: tools/build-deps.sh [--prefix DIR]" \
                 "[--build-type TYPE] [--clean]"
            exit 0
            ;;
        *)
            echo "build-deps: unknown option: $1" >&2
            exit 2
            ;;
    esac
done

if [ "$clean" -eq 1 ]; then
    echo "== clean"
    rm -rf "$root/build/deps" "$prefix"
fi

mkdir -p "$prefix"
# Absolute, so the CMake cache key and the hint the top-level build passes
# to find_package are the same string.
prefix=$(CDPATH= cd -- "$prefix" && pwd)

# build_one <source dir> <label> [cmake args...]
build_one() {
    src=$1
    label=$2
    shift 2
    if [ ! -f "$src/CMakeLists.txt" ]; then
        echo "build-deps: no CMakeLists.txt in $src" >&2
        echo "            (run tools/fetch-deps.sh first)" >&2
        exit 1
    fi
    bdir="$root/build/deps/$label"
    echo "== $label: configure"
    cmake -S "$src" -B "$bdir" \
        -DCMAKE_BUILD_TYPE="$build_type" \
        -DCMAKE_INSTALL_PREFIX="$prefix" \
        "$@"
    echo "== $label: build"
    cmake --build "$bdir" --parallel
    echo "== $label: install"
    cmake --install "$bdir"
}

# shrewd-basic: static only, no tests or command-line programs - we want
# the library and its package config, nothing else.
build_one "$root/deps/shrewd-basic" shrewd-basic \
    -DSHREWD_BASIC_INSTALL=ON \
    -DSHREWD_BASIC_BUILD_STATIC=ON \
    -DSHREWD_BASIC_BUILD_SHARED=OFF \
    -DSHREWD_BASIC_BUILD_TESTS=OFF \
    -DSHREWD_BASIC_BUILD_PROGRAMS=OFF

# lvgl: LV_CONF_SKIP makes the build self-contained - no lv_conf.h to
# supply, and lvgl writes the define into its install interface so every
# consumer compiles against the same configuration.
#
# Demos, examples and ThorVG default to ON upstream and are pure ballast
# here. LV_FETCH_DEPENDENCIES is the fuse: with it OFF a build that would
# have reached for the network fails loudly instead of quietly fetching a
# dependency that is not pinned in versions.lock.
build_one "$root/deps/lvgl" lvgl \
    -DLV_BUILD_INSTALL=ON \
    -DLV_CONF_SKIP=ON \
    -DLV_BUILD_TESTS=OFF \
    -DCONFIG_LV_BUILD_DEMOS=OFF \
    -DCONFIG_LV_BUILD_EXAMPLES=OFF \
    -DCONFIG_LV_USE_THORVG=OFF \
    -DLV_FETCH_DEPENDENCIES=OFF

echo "dependencies installed to $prefix"
echo "next: cmake -S . -B build -DCMAKE_PREFIX_PATH=$prefix"
