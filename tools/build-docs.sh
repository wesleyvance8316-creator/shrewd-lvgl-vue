#!/bin/sh
# Generate the API docs with doxygen (tools/build-docs.sh).
#
# Output: build/docs/html/index.html
#
# doxygen is a system tool; if it is missing we stop and say how to get it
# rather than quietly skipping the docs - a silent skip is how a project
# ends up with a README that links to pages nobody can build.
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(dirname "$here")

if ! command -v doxygen >/dev/null 2>&1; then
    echo "doxygen is not installed." >&2
    echo "It turns the header comments and docs/*.md into the HTML reference." >&2
    echo "Install it and re-run, e.g.:" >&2
    echo "    ! sudo apt install -y doxygen graphviz" >&2
    echo "(graphviz is optional: without it the docs just have no call/include graphs.)" >&2
    exit 1
fi

cd "$root"

# HAVE_DOT 交给这里决定:机器上有 graphviz 就画图,没有就安静地不画,
# 免得刷一屏 "problems opening map file"。
cfg=build/docs/Doxyfile.run
mkdir -p build/docs
{
    cat doxygen/Doxyfile
    if command -v dot >/dev/null 2>&1; then
        echo "HAVE_DOT = YES"
    else
        echo "HAVE_DOT = NO"
    fi
} > "$cfg"
doxygen "$cfg"

if [ ! -f build/docs/html/index.html ]; then
    echo "doxygen ran but produced no index.html" >&2
    exit 1
fi

echo "docs: $root/build/docs/html/index.html"
