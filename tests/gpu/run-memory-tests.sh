#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
no_pie=
if [ "$(uname -s)" = Linux ]; then no_pie=-no-pie; fi
"${CXX:-clang++}" -std=c++17 -g -O1 -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer $no_pie \
  -DNATIVE_ADRENO_HOST_TEST -I"$root/tests/gpu" -I"$root/gpu/NativeAdreno" \
  "$root/tests/gpu/memory_native_test.cpp" "$root/gpu/NativeAdreno/NativeAdrenoMemory.cpp" \
  -o "$tmp/memory-test"
UBSAN_OPTIONS=halt_on_error=1 "$tmp/memory-test"
UBSAN_OPTIONS=halt_on_error=1 "$tmp/memory-test" pool-quarantine
"${CC:-clang}" -std=c11 -g -O1 -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer $no_pie \
  "$root/tests/gpu/hfi_startup_test.c" "$root/gpu/AdrenoFirmware/AdrenoHfiStartup.c" \
  -o "$tmp/hfi-startup-test"
UBSAN_OPTIONS=halt_on_error=1 "$tmp/hfi-startup-test"
