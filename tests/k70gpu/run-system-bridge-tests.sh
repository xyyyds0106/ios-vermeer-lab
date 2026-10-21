#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
TMPDIR="$root/../build/tmp"
mkdir -p "$TMPDIR"
export TMPDIR
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
no_pie=
if [ "$(uname -s)" = Linux ]; then no_pie=-no-pie; fi
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer $no_pie \
    "$root/tests/k70gpu/system_bridge_test.c" \
    "$root/gpu/K70/K70GpuSystemBridge.c" -o "$tmp/system_bridge"
UBSAN_OPTIONS=halt_on_error=1 "$tmp/system_bridge"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g \
    -DNATIVE_ADRENO_HOST_TEST -I"$root/tests/k70gpu/shims" -I"$root/tests/gpu" \
    -I"$root/gpu/ArmSmmu4K" -I"$root/gpu/AdrenoPM4" -I"$root/gpu/AdrenoFirmware" \
    -fsanitize=address,undefined -fno-omit-frame-pointer $no_pie \
    "$root/tests/k70gpu/system_bridge_native_test.cpp" -o "$tmp/native_evidence"
UBSAN_OPTIONS=halt_on_error=1 "$tmp/native_evidence"
