#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
TMPDIR="$root/../build/tmp"
mkdir -p "$TMPDIR"
export TMPDIR
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror -g -O1 \
    -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
    "$root/tests/k70gpu/board_policy_test.c" "$root/gpu/K70/K70GpuBoardPolicy.c" -o "$tmp/policy"
UBSAN_OPTIONS=halt_on_error=1 "$tmp/policy" "${1:-$root/../device-audit/live.dtb}"
