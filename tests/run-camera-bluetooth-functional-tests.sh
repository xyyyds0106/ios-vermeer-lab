#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
build_dir=$(mktemp -d)
trap 'rm -f "$build_dir/camera_cci" "$build_dir/bt_uart_gpi" "$build_dir/bt_uart_service" "$build_dir/service.inc"; rmdir "$build_dir"' EXIT HUP INT TERM
set --
case "$(uname -s)" in Linux) set -- -fno-pie -no-pie ;; esac
for test in camera_cci bt_uart_gpi; do
    "${CC:-clang}" -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
        "$@" "tests/${test}_test.c" -o "$build_dir/$test"
    "$build_dir/$test"
done
# Compile the actual native callbacks and one worker iteration against XNU
# API mocks. Remove only platform-header includes; preserve all source logic.
"${PYTHON:-python3}" -c 'import pathlib,sys; p=pathlib.Path("xnu/vermeer/media/vermeer_bt_uart_service.c"); pathlib.Path(sys.argv[1]).write_text("\n".join(x for x in p.read_text().splitlines() if not x.startswith("#include <"))+"\n")' "$build_dir/service.inc"
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    "$@" -I xnu/vermeer/media -DVBUS_SOURCE=\""$build_dir/service.inc"\" \
    tests/bt_uart_service_test.c -o "$build_dir/bt_uart_service"
"$build_dir/bt_uart_service"
