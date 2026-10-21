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
for test in frame_policy goodix_packet geni_spi geni_dma touch_clock touch_route gpi gpi_transport; do
  "${CC:-clang}" -std=c11 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer $no_pie \
    "$root/tests/${test}_test.c" -o "$tmp/$test"
  UBSAN_OPTIONS=halt_on_error=1 "$tmp/$test"
done
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer $no_pie \
  -I"$root/gpu/ArmSmmu4K" "$root/tests/touch_iommu_test.c" \
  "$root/gpu/ArmSmmu4K/ArmSmmu4K.c" \
  "$root/gpu/ArmSmmu4K/ArmSmmu4KContext.c" -o "$tmp/touch_iommu"
UBSAN_OPTIONS=halt_on_error=1 "$tmp/touch_iommu"
sh "$root/tests/gpu/run-memory-tests.sh"
sh "$root/tests/run-rpmh-touch-power-tests.sh"
sh "$root/tests/k70gpu/run-core-tests.sh"
