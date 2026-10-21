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
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    "$root/tests/k70gpu/power_provider_test.c" \
    "$root/gpu/AdrenoFirmware/AdrenoCmdDb.c" \
    "$root/gpu/AdrenoFirmware/AdrenoHfiVotes.c" \
    "$root/gpu/AdrenoFirmware/AdrenoHfiStartup.c" -o "$tmp/power"
UBSAN_OPTIONS=halt_on_error=1 "$tmp/power"
