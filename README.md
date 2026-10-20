# vermeer XNU bring-up lab

Private experiment for Redmi K70 (vermeer / SM8550).

The initial workflow reproduces the **SWEET reference build** from
https://github.com/dipa-studio/ios-sweet at
11da58bd050672960c0a83b5bef2176047eea479. It is **not a K70 kernel** and must
not be booted or flashed on vermeer. The patch authors retain their copyright
and the original source and dependency licensing applies.

Confirmed independently on one K70: temporary boot v4 diagnostic image,
linear framebuffer output, EL1 execution, 19.2 MHz counter, 16KB MMU and caches.
XNU and iOS have not been booted on this device. Interrupt delivery, secondary
cores and storage drivers remain unverified.

The workflow is manual-only, has read-only repository permissions and a
45-minute job limit. It does not contain Apple firmware or device dumps.
