#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p out
for source in entry.S vectors.S main.c console.c loader.c fdt.c; do
    aarch64-linux-gnu-gcc -O2 -ffreestanding -nostdlib -fno-pic -fno-pie \
        -mgeneral-regs-only -mstrict-align -fno-stack-protector -Wall -Wextra -Werror \
        -c "$source" -o "out/${source%.*}.o"
done
if aarch64-linux-gnu-readelf -r out/main.o out/console.o out/loader.o out/fdt.o | grep -q ABS64; then
    echo 'Absolute C relocation: refusing build' >&2
    exit 1
fi
aarch64-linux-gnu-ld -T link.ld --build-id=none out/entry.o out/vectors.o \
    out/main.o out/console.o out/loader.o out/fdt.o -o out/shim.elf
aarch64-linux-gnu-objcopy -O binary out/shim.elf out/Image
size=$(aarch64-linux-gnu-nm out/shim.elf | awk '$3=="_end" {print "0x"$1}')
truncate -s "$((size))" out/Image
echo 'Built vermeer boot shim (not a kernel)'
