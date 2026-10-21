#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT HUP INT TERM
${CC:-clang-14} -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -g -no-pie tests/rpmh_observe_test.c -o "$out/observe"
UBSAN_OPTIONS=halt_on_error=1 "$out/observe"
${CC:-clang-14} -target aarch64-none-elf -ffreestanding -std=c11 -Wall -Wextra -Werror \
    -fsyntax-only -include xnu/vermeer/vermeer_rpmh_observe.h -x c /dev/null
