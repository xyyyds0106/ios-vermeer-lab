#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT HUP INT TERM
${CC:-clang-14} -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -g -no-pie tests/rpmh_touch_power_test.c -o "$out/power"
UBSAN_OPTIONS=halt_on_error=1 "$out/power"
${CC:-clang-14} -target aarch64-none-elf -ffreestanding -std=c11 -Wall -Wextra -Werror \
    -fsyntax-only -include xnu/vermeer/vermeer_rpmh_touch_native.h -x c /dev/null
