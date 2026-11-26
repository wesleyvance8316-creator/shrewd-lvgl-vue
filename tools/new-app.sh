#!/bin/sh
# Create a new application from template/app (tools/new-app.sh).
#
# The template is the "vue flavour" the project promises (docs/design.md
# §9): a known directory shape, a `main.c` that reads like `main.ts`
# (init -> use -> mount -> step), and a standalone build that runs out of
# the box. The GUI is not required to be its own process - pass
# --thread to generate the entry for "GUI as a thread inside a bigger
# program" (design.md §9.2).
#
# Usage: tools/new-app.sh <name> [options]
#   --dir DIR        where to put it (default: cwd)
#   --thread         generate the thread entry instead of main()
#   --bare           minimal template: no example view
#   --force          overwrite an existing target directory
#
# The name becomes the project name, the C identifier prefix and the
# header guard, so keep it to [A-Za-z0-9_].
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(dirname "$here")

name=""
out_dir=$(pwd)
thread=0
bare=0
force=0

while [ $# -gt 0 ]; do
    case "$1" in
        --dir)
            out_dir=$2
            shift 2
            ;;
        --thread)
            thread=1
            shift
            ;;
        --bare)
            bare=1
            shift
            ;;
        --force)
            force=1
            shift
            ;;
        -h | --help)
            echo "usage: tools/new-app.sh <name> [--dir DIR] [--thread] [--bare] [--force]"
            exit 0
            ;;
        -*)
            echo "new-app: unknown option: $1" >&2
            exit 2
            ;;
        *)
            if [ -n "$name" ]; then
                echo "new-app: more than one name given: $name and $1" >&2
                exit 2
            fi
            name=$1
            shift
            ;;
    esac
done

if [ -z "$name" ]; then
    echo "new-app: missing <name>" >&2
    echo "usage: tools/new-app.sh <name> [--dir DIR] [--thread] [--bare] [--force]" >&2
    exit 2
fi

case "$name" in
    *[!A-Za-z0-9_]*)
        echo "new-app: name must be [A-Za-z0-9_]: $name" >&2
        exit 2
        ;;
esac

template="$root/template/app"
if [ ! -f "$template/CMakeLists.txt" ]; then
    echo "new-app: no template at $template" >&2
    exit 1
fi

target="$out_dir/$name"
if [ -e "$target" ]; then
    if [ "$force" -ne 1 ]; then
        echo "new-app: $target already exists (use --force to overwrite)" >&2
        exit 1
    fi
    rm -rf "$target"
fi

# Name-derived identifiers. The naming law (docs/initiation.md §5) is about
# shrewd_lv_view_*; an application carries its own prefix.
cname=$(printf '%s' "$name" | tr 'A-Z-' 'a-z_')
guard=$(printf '%s' "$cname" | tr 'a-z' 'A-Z')
prefix="$cname"

mkdir -p "$target"
( cd "$template" && tar cf - . ) | ( cd "$target" && tar xf - )

# Substitute the placeholders in every text file.
find "$target" -type f | while read -r f; do
    case "$f" in
        *.png | *.jpg | *.bin) continue ;;
    esac
    sed -i \
        -e "s|@APP_NAME@|$name|g" \
        -e "s|@APP_CNAME@|$cname|g" \
        -e "s|@APP_GUARD@|$guard|g" \
        -e "s|@APP_PREFIX@|$prefix|g" \
        "$f"
done

# --thread: the entry is gui_thread(), not main().
if [ "$thread" -eq 1 ]; then
    rm -f "$target/src/main.c"
    sed -i \
        -e 's|^option(SHREWD_APP_AS_THREAD.*$|option(SHREWD_APP_AS_THREAD|' \
        -e 's|^        "Build as a library exposing gui_thread() instead of a standalone main()"|        "Build as a library exposing gui_thread() instead of a standalone main()"|' \
        -e 's|^        OFF)|        ON)|' \
        "$target/CMakeLists.txt"
else
    rm -f "$target/src/app_thread.c"
fi

# --bare: drop the example view, keep the empty layout.
if [ "$bare" -eq 1 ]; then
    rm -rf "$target/src/views/home.c" "$target/src/views/home.h"
fi

echo "created $target"
echo
echo "next:"
echo "  cd $target"
echo "  tools/build-deps.sh        # from the framework repo, once"
echo "  cmake -S . -B build && cmake --build build"
if [ "$thread" -eq 1 ]; then
    echo "  # then link $cname into your program and start gui_thread()"
else
    echo "  ./build/$cname"
fi
