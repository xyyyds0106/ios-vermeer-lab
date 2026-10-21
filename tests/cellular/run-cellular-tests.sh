#!/bin/sh
# Offline only. No sockets, device files or native radio backend.
set -eu
cd "$(dirname "$0")/../.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT HUP INT TERM
for name in cellular_service cellular_qrtr; do
    "${CC:-clang-14}" -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g \
        -fsanitize=address,undefined -fno-omit-frame-pointer -no-pie \
        "tests/cellular/${name}_test.c" -o "$out/$name"
    UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=1 "$out/$name"
done
for header in vermeer_cellular.h vermeer_cellular_qrtr.h; do
    "${CC:-clang-14}" -target aarch64-none-elf -ffreestanding -std=c11 \
        -Wall -Wextra -Werror -pedantic -fsyntax-only \
        -include "xnu/vermeer/cellular/$header" -x c /dev/null
done
echo 'SYNTHETIC ONLY: ASan/UBSan and freestanding AArch64 syntax: PASS'
