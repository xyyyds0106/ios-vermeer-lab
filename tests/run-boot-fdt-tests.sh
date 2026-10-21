#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TMPDIR="$root/../build/tmp"
mkdir -p "$TMPDIR"
export TMPDIR
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
no_pie=
if [ "$(uname -s)" = Linux ]; then no_pie=-no-pie; fi
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer $no_pie \
  "$root/tests/boot_fdt_test.c" -o "$tmp/boot_fdt"
UBSAN_OPTIONS=halt_on_error=1 "$tmp/boot_fdt"
"${CC:-clang}" -target aarch64-none-elf -ffreestanding -std=c11 \
  -Wall -Wextra -Werror -fsyntax-only -include "$root/boot/boot_fdt.h" -x c /dev/null
