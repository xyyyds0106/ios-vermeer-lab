#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
tmp=$(mktemp -d)
trap 'rm -f "$tmp/fill"; rmdir "$tmp"' EXIT HUP INT TERM
set --
case "$(uname -s)" in Linux) set -- -fno-pie -no-pie ;; esac
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined "$@" \
    tests/translation/fill_plan_test.c gpu/translation/AdrenoFillPlan.c gpu/AdrenoPM4/AdrenoPM4.c -o "$tmp/fill"
"$tmp/fill"
