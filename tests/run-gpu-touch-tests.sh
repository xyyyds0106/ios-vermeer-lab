#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
no_pie=
if [ "$(uname -s)" = Linux ]; then no_pie=-no-pie; fi
for test in goodix_packet geni_spi geni_dma touch_clock touch_route; do
  "${CC:-clang}" -std=c11 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer $no_pie \
    "$root/tests/${test}_test.c" -o "$tmp/$test"
  UBSAN_OPTIONS=halt_on_error=1 "$tmp/$test"
done
sh "$root/tests/gpu/run-memory-tests.sh"
