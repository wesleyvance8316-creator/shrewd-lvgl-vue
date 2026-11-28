#!/bin/sh
# Style gate for shrewd-lvgl-vue.
#
#   1. clang-format - every .c/.h must match .clang-format (*.h.in
#      templates are excluded: they carry @VAR@ placeholders).
#   2. clang-tidy  - .clang-tidy rules (initializer completeness,
#      braces around statements).
#   3. condition parens - a multi-operand condition wraps each
#      sub-condition in parentheses (tools/check-condition-parens.py).
#
# Usage: tools/check-style.sh
#   BUILD=<dir>          compile database for clang-tidy (default: build)
#   CLANG_FORMAT=<bin>   formatter to use (default: clang-format)
#   CLANG_TIDY=<bin>     checker to use   (default: clang-tidy)
#
# The two overrides exist because clang-format's output is version-specific:
# CI pins the same version that produced the committed formatting, otherwise
# the gate reports "drift" that is really just a toolchain difference.
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(dirname "$here")
build="${BUILD:-build}"
fail=0

cd "$root"

echo "== clang-format"
for f in $(find library include tests tools \
               \( -name '*.c' -o -name '*.h' \) | sort); do
    case "$f" in
        *.in) continue ;;
        tools/reflect_gen_golden*) continue ;;  # machine output, not hand code
    esac
    if ! "${CLANG_FORMAT:-clang-format}" --dry-run --Werror "$f" > /dev/null 2>&1; then
        echo "format drift: $f"
        fail=1
    fi
done

echo "== clang-tidy"
tidy_args=""
if [ -f "$build/compile_commands.json" ]; then
    tidy_args="-p $build"
fi
for f in $(find library tests tools -name '*.c' | sort); do
    case "$f" in
        tools/reflect_gen_golden*) continue ;;  # machine output, not hand code
    esac
    out=$("${CLANG_TIDY:-clang-tidy}" $tidy_args "$f" -- \
              -Iinclude -Iinclude/shrewd-basic-internal -Ilibrary/reflect \
              -std=c11 2> /dev/null \
          | grep -E 'warning: .*\[(cppcoreguidelines-init-variables|readability-braces-around-statements)\]' \
          || true)
    if [ -n "$out" ]; then
        echo "$out"
        fail=1
    fi
done

echo "== condition parens"
if ! python3 "$here/check-condition-parens.py"; then
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "style check FAILED"
    exit 1
fi
echo "style check passed"
