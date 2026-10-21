#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
no_pie=
if [ "$(uname -s)" = Linux ]; then no_pie=-no-pie; fi
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer $no_pie \
  "$root/tests/gpi_transport_test.c" -o "$tmp/gpi"
UBSAN_OPTIONS=halt_on_error=1 "$tmp/gpi"
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer $no_pie \
  -I"$root/gpu/ArmSmmu4K" "$root/tests/touch_iommu_test.c" \
  "$root/gpu/ArmSmmu4K/ArmSmmu4K.c" "$root/gpu/ArmSmmu4K/ArmSmmu4KContext.c" \
  -o "$tmp/iommu"
UBSAN_OPTIONS=halt_on_error=1 "$tmp/iommu"
"${CC:-clang}" -target aarch64-none-elf -ffreestanding -std=c11 \
  -Wall -Wextra -Werror -fsyntax-only -I"$root/gpu/ArmSmmu4K" \
  -include "$root/xnu/vermeer/vermeer_gpi_transport.h" \
  -include "$root/xnu/vermeer/vermeer_touch_iommu.h" -x c /dev/null
echo 'Functional touch cores: freestanding AArch64 PASS'
