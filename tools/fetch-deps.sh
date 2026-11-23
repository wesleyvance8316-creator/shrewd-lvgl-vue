#!/bin/sh
# Place the dependency source trees under deps/ (tools/fetch-deps.sh).
#
# Third-party architecture (docs/initiation.md): every external library is
# built as its own project and consumed through a standard CMake package
# import - never add_subdirectory, never FetchContent into the build tree.
# This script only puts the sources in place; tools/build-deps.sh is the
# one that compiles and installs them into a common prefix.
#
# Usage: tools/fetch-deps.sh [options]
#   --basic-from local|git   where shrewd-basic comes from (default: local)
#   --basic-src DIR          local checkout to copy
#                            (default: <root>/../shrewd-basic)
#   --lock FILE              version lock to read
#                            (default: <root>/deps/versions.lock)
#   --deps-dir DIR           where the trees are written (default: <root>/deps)
#   --force                  replace an existing tree instead of keeping it
#
# The lock says what to fetch - that is repo policy, so it stays at its
# default even when --deps-dir points somewhere else. lvgl is always
# cloned at its pinned git tag; shrewd-basic defaults to the local sibling
# checkout so the pre-1.0 API loop stays cheap - pass --basic-from git
# when a pinned third-party snapshot is what you want.
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(dirname "$here")
deps_dir="$root/deps"
lock="$root/deps/versions.lock"
basic_from=local
basic_src=""
force=0

while [ $# -gt 0 ]; do
    case "$1" in
        --basic-from)
            basic_from=$2
            shift 2
            ;;
        --basic-src)
            basic_src=$2
            shift 2
            ;;
        --lock)
            lock=$2
            shift 2
            ;;
        --deps-dir)
            deps_dir=$2
            shift 2
            ;;
        --force)
            force=1
            shift
            ;;
        -h | --help)
            echo "usage: tools/fetch-deps.sh [--basic-from local|git]" \
                 "[--basic-src DIR] [--lock FILE] [--deps-dir DIR] [--force]"
            exit 0
            ;;
        *)
            echo "fetch-deps: unknown option: $1" >&2
            exit 2
            ;;
    esac
done

case "$basic_from" in
    local | git) ;;
    *)
        echo "fetch-deps: --basic-from must be local or git, got: $basic_from" >&2
        exit 2
        ;;
esac

if [ ! -f "$lock" ]; then
    echo "fetch-deps: missing version lock: $lock" >&2
    exit 1
fi

# lock_get <key>: the value of a KEY=VALUE line in versions.lock. The file
# is parsed, never sourced, so a stray line cannot run anything.
lock_get() {
    val=$(grep -E "^$1=" "$lock" | tail -n 1 | cut -d= -f2-)
    if [ -z "$val" ]; then
        echo "fetch-deps: $1 is not set in $lock" >&2
        exit 1
    fi
    printf '%s\n' "$val"
}

# Every tree is written to <dest>.partial and renamed into place only once
# it is complete. A fetch that dies halfway therefore leaves no tree behind
# for a later run to keep and trust.
pending=""
trap 'if [ -n "$pending" ]; then rm -rf "$pending"; fi' EXIT

# take <dest> <label>: 0 when the tree should be written, 1 when an
# existing tree is kept. --force is what replaces it.
take() {
    if [ -e "$1" ] && [ "$force" -ne 1 ]; then
        echo "-- $2: already present ($1), keeping it (--force to replace)"
        return 1
    fi
    rm -rf "$1" "$1.partial"
    pending="$1.partial"
    return 0
}

promote() {
    mv "$pending" "$1"
    pending=""
}

# place_ref <url> <ref> <commit> <dest> <label>
# <ref> is a tag/branch name or a full commit hash - git clone --branch
# takes only the former, so a pinned commit is shallow-fetched instead.
# Either way the result must land exactly on <commit>.
place_ref() {
    echo "-- $5: clone $1 @ $2"
    if printf '%s' "$2" | grep -qE '^[0-9a-f]{40}$'; then
        mkdir -p "$4"
        git -C "$4" init --quiet
        git -C "$4" remote add origin "$1"
        git -C "$4" fetch --quiet --depth 1 origin "$2"
        git -C "$4" checkout --quiet --detach FETCH_HEAD
    else
        # A pin is meant to land detached; that is not news worth printing.
        git -c advice.detachedHead=false clone --quiet --depth 1 \
            --branch "$2" "$1" "$4"
    fi
    got=$(git -C "$4" rev-parse HEAD)
    if [ "$got" != "$3" ]; then
        echo "fetch-deps: $5 commit drift - lock says $3, checkout is $got" >&2
        exit 1
    fi
    echo "   $5 @ $got"
}

# place_local <src> <dest> <label>
place_local() {
    if [ ! -d "$1" ]; then
        echo "fetch-deps: $3 source not found: $1" >&2
        echo "            (pass --basic-src DIR to point somewhere else)" >&2
        exit 1
    fi
    echo "-- $3: copy $1 -> $2"
    mkdir -p "$2"
    # The working tree, minus VCS/IDE/build litter and any nested deps/
    # (a checkout pointed at this repo must not copy our own dependencies).
    ( cd "$1" && tar cf - \
        --exclude=.git \
        --exclude=.idea \
        --exclude=build \
        --exclude='cmake-build-*' \
        --exclude=deps \
        . ) | ( cd "$2" && tar xf - )
    if git -C "$1" rev-parse --git-dir >/dev/null 2>&1; then
        echo "   $3 @ $(git -C "$1" rev-parse HEAD) (working tree)"
    fi
}

mkdir -p "$deps_dir"

if take "$deps_dir/lvgl" lvgl; then
    place_ref "$(lock_get LVGL_GIT_URL)" \
              "$(lock_get LVGL_GIT_TAG)" \
              "$(lock_get LVGL_GIT_COMMIT)" \
              "$pending" lvgl
    promote "$deps_dir/lvgl"
fi

if take "$deps_dir/shrewd-basic" shrewd-basic; then
    if [ "$basic_from" = git ]; then
        place_ref "$(lock_get SHREWD_BASIC_GIT_URL)" \
                  "$(lock_get SHREWD_BASIC_GIT_COMMIT)" \
                  "$(lock_get SHREWD_BASIC_GIT_COMMIT)" \
                  "$pending" shrewd-basic
    else
        src=$basic_src
        if [ -z "$src" ]; then
            src="$root/../shrewd-basic"
        fi
        place_local "$src" "$pending" shrewd-basic
    fi
    promote "$deps_dir/shrewd-basic"
fi

echo "dependencies in place under $deps_dir"
echo "next: tools/build-deps.sh"
