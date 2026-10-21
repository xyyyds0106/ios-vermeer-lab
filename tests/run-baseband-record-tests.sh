#!/bin/sh
# Offline only. The temporary directory contains synthetic records, not logs.
set -eu
cd "$(dirname "$0")/.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT HUP INT TERM
${CC:-clang-14} -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -g -no-pie tests/baseband_record_test.c -o "$out/record-test"
UBSAN_OPTIONS=halt_on_error=1 "$out/record-test"
UBSAN_OPTIONS=halt_on_error=1 "$out/record-test" --emit > "$out/synthetic.log"
python3 -B tests/baseband_records_test.py "$out/synthetic.log"
${CC:-clang-14} -target aarch64-none-elf -ffreestanding -std=c11 -Wall -Wextra -Werror \
    -fsyntax-only -include xnu/vermeer/vermeer_baseband_record.h -x c /dev/null
