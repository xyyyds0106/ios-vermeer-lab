#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
build_dir=$(mktemp -d)
trap 'rm -f "$build_dir/bt_h4"; rmdir "$build_dir"' EXIT HUP INT TERM
set --
# Clang 14's ASan can fault before initialization with a randomized PIE on
# recent WSL kernels. Fix only this host test's load address; keep both
# sanitizers enabled. Darwin uses its normal executable/linker defaults.
case "$(uname -s)" in Linux) set -- -fno-pie -no-pie ;; esac
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined "$@" tests/bt_h4_test.c -o "$build_dir/bt_h4"
"$build_dir/bt_h4"
"${PYTHON:-python3}" tests/media_inventory_test.py
