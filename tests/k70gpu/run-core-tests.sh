#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
for f in gpu/ArmSmmu4K/*.c gpu/AdrenoPM4/*.c; do
  case "$f" in *Mmio.c) continue ;; esac
  "${CC:-clang}" -std=c11 -g -O1 -fsanitize=address,undefined -c "$f" -o "$tmp/$(basename "$f").o"
done
"${CXX:-clang++}" -std=c++17 -g -O1 -Wall -Wextra -Werror \
  -Wno-c99-designator -Wno-reorder-init-list -fsanitize=address,undefined -fno-pie -no-pie \
  -DNATIVE_ADRENO_HOST_TEST -Itests/k70gpu/shims -Itests/gpu -Igpu/NativeAdreno -Igpu/K70 -Igpu/ArmSmmu4K -Igpu/AdrenoPM4 \
  tests/k70gpu/core_negative_test.cpp gpu/K70/K70Gpu.cpp \
  gpu/NativeAdreno/NativeAdrenoMemory.cpp gpu/NativeAdreno/NativeAdrenoSmmuMemory.cpp \
  gpu/NativeAdreno/NativeAdrenoSmmuContext.cpp gpu/NativeAdreno/NativeAdrenoVm.cpp \
  gpu/NativeAdreno/NativeAdrenoRing.cpp gpu/NativeAdreno/NativeAdrenoCpInit.cpp \
  gpu/NativeAdreno/NativeAdrenoIrq.cpp gpu/NativeAdreno/NativeAdrenoPwrup.cpp \
  gpu/NativeAdreno/NativeAdrenoSha256.cpp gpu/NativeAdreno/NativeAdrenoFirmware.cpp "$tmp"/*.o -o "$tmp/core"
UBSAN_OPTIONS=halt_on_error=1 "$tmp/core"
"${CC:-clang}" -std=c11 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-pie -no-pie \
  tests/k70gpu/clock_test.c gpu/K70/K70GpuClock.c -o "$tmp/clock"
UBSAN_OPTIONS=halt_on_error=1 "$tmp/clock"
"${CC:-clang}" -std=c11 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-pie -no-pie \
  tests/k70gpu/socinfo_test.c -o "$tmp/socinfo"
UBSAN_OPTIONS=halt_on_error=1 "$tmp/socinfo"
"${CXX:-clang++}" -std=c++17 -g -O1 -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-pie -no-pie -DNATIVE_ADRENO_HOST_TEST \
  -Itests/k70gpu/shims -Itests/gpu -Igpu/NativeAdreno -Igpu/AdrenoPM4 \
  tests/k70gpu/ring_test.cpp gpu/NativeAdreno/NativeAdrenoRing.cpp "$tmp/AdrenoPM4.c.o" -o "$tmp/ring"
UBSAN_OPTIONS=halt_on_error=1 "$tmp/ring"
for f in gpu/AdrenoFirmware/AdrenoFirmware.c gpu/AdrenoFirmware/AdrenoHfiStartup.c gpu/K70/K70Gmu.c gpu/K70/K70GpuClock.c; do
  "${CC:-clang}" -std=c11 -g -O1 -Wall -Wextra -Werror -DK70_GMU_HOST_TEST \
    -Igpu/AdrenoFirmware -fsanitize=address,undefined -c "$f" -o "$tmp/$(basename "$f").o"
done
"${CXX:-clang++}" -std=c++17 -g -O1 -Wall -Wextra -Werror \
  -Wno-c99-designator -Wno-reorder-init-list -fsanitize=address,undefined -fno-pie -no-pie \
  -DNATIVE_ADRENO_HOST_TEST -Itests/k70gpu/shims -Itests/gpu -Igpu/NativeAdreno -Igpu/K70 -Igpu/ArmSmmu4K -Igpu/AdrenoFirmware -Igpu/AdrenoPM4 \
  tests/k70gpu/gmu_test.cpp tests/k70gpu/host_memory.cpp gpu/K70/K70GmuNative.cpp gpu/K70/K70Zap.cpp gpu/K70/K70GpuNative.cpp gpu/K70/K70Gpu.cpp \
  gpu/NativeAdreno/NativeAdrenoMemory.cpp gpu/NativeAdreno/NativeAdrenoSmmuMemory.cpp \
  gpu/NativeAdreno/NativeAdrenoSmmuContext.cpp gpu/NativeAdreno/NativeAdrenoVm.cpp \
  gpu/NativeAdreno/NativeAdrenoGmuMemory.cpp gpu/NativeAdreno/NativeAdrenoGmuContext.cpp \
  gpu/NativeAdreno/NativeAdrenoRing.cpp gpu/NativeAdreno/NativeAdrenoCpInit.cpp gpu/NativeAdreno/NativeAdrenoPwrup.cpp gpu/NativeAdreno/NativeAdrenoIrq.cpp \
  gpu/NativeAdreno/NativeAdrenoSha256.cpp gpu/NativeAdreno/NativeAdrenoFirmware.cpp "$tmp"/*.o -o "$tmp/gmu"
UBSAN_OPTIONS=halt_on_error=1 "$tmp/gmu"
for failure in 1 2 3 4 5; do
  UBSAN_OPTIONS=halt_on_error=1 "$tmp/gmu" "$failure"
done
