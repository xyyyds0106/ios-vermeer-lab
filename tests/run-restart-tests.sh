#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
no_pie=
if [ "$(uname -s)" = Linux ]; then no_pie=-no-pie; fi
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -fno-omit-frame-pointer $no_pie "$root/tests/restart_control_test.c" -o "$tmp/restart"
UBSAN_OPTIONS=halt_on_error=1 "$tmp/restart"
