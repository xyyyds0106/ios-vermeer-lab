# K70 Bluetooth and camera observation contract

Bluetooth has a bounded standard version query and a read-only register
observation interface, with host fault tests. Neither is linked into XNU or
connected to a live transport. Camera still needs a verified sensor identity
and power sequence before CCI or image capture can be implemented. The next
device observation belongs in the existing XNU CDC collection path; this lane
does not start a separate probe or take the device port.

## Bluetooth selection and UART mode

The saved K70 FDT includes both `qcom,kiwi` and `qcom,qca6490`. The pinned
[downstream power driver](https://github.com/LineageOS/android_kernel_xiaomi_sm8550-modules/blob/92a9fde479d3262da080cfd28bab0775b0da500a/qcom/opensource/bt-kernel/pwr/btpower.c#L1253)
requests HSP/qca6490 for GPIO83 high and HMT/kiwi for low through
`cnss_utils_update_device_type`. The
[CNSS helper](https://github.com/LineageOS/android_kernel_xiaomi_sm8550-modules/blob/92a9fde479d3262da080cfd28bab0775b0da500a/qcom/opensource/wlan/platform/cnss_utils/cnss_utils.c)
retains the first selected type, which WLAN may already have set, and returns
that type. The power driver chooses its child/regulator configuration from
the returned value. This is a board-compatible reference source; the retained
backups do not establish its identity with the installed vendor module or
contain the original selection record or a controller version response.

`vermeer_bt_observe.h` accepts two GPIO83 configuration/input samples and
returns a selection hint only when the pin is in GPIO input mode, its
configuration is unchanged, and both input bits agree. A hint is not a silicon
identity: an existing CNSS selection can override the hint, and a later boot
can leave the pin in a different state. Do not select firmware or enable rails
from this hint alone.

The high-speed alias maps to wrapper2 SE6 at parent bus address `0x898000`,
with GPIO76/77/78/79 for CTS/RTS/TX/RX and clock binding 123. Debug serial is a
different instance at `0xa9c000`. The pinned
[HS UART driver](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/53ef33eacdc4f7ac737451e4cfc91963d59e3956/drivers/tty/serial/msm_geni_serial.c#L4309)
uses `GENI_IF_DISABLE_RO & 1` to select GSI/GPI DMA. When FIFO is available,
its normal non-console path selects SE DMA. Consequently a FIFO-disabled
Bluetooth engine must not inherit the touch driver's internal DMA assumptions.
The observer reports the mode without granting ownership or transfer readiness.

Both candidate configurations use BT enable GPIO81 and SW control GPIO82.
Their rail voltages and PDC tables differ. Resolve the selection and establish
RPMh/clock/reset ownership before implementing those writes. The existing
FDT inventory preserves the per-candidate resources in a private report.

## Fields for the next CDC capture

The header exports `vbt_obs_registers`, `vbt_obs_names`, and a validity mask.
Its fixed plan contains eight TLMM reads and 22 optional UART reads. No FIFO
data, interrupt clear, command, firmware download or PMIC register is accessed.

The platform adapter must validate the K70 physical model, retained DT resource
bounds, mapping attributes and accessible power/clock domain before supplying
`vbt_observe_bank` with an access proof. Keep the required ownership/domain
references throughout collection. UART access is skipped when that proof is
unavailable. `ml_probe_read` is not a fault-safe substitute on this XNU.

| Bank | CDC fields | Read plan with hexadecimal offsets |
| --- | --- | --- |
| TLMM | `strap_cfg_a`, `strap_io_a`, `strap_cfg_b`, `strap_io_b` | GPIO83 control/input twice, offsets 153000/153004 within the verified TLMM window |
| TLMM | `bt_enable_cfg`, `bt_enable_io`, `bt_sw_cfg`, `bt_sw_io` | GPIO81 control/input 151000/151004 and GPIO82 control/input 152000/152004 |
| UART | `fw_m`, `fw_s`, `if_disable`, `dma_mode`, `status` | Offsets 68/6c/64/258/40 in the verified SE6 window |
| UART | `clk_m`, `clk_s`, `clk_sel` | Offsets 48/4c/7c |
| UART | `hw_tx`, `hw_rx`, `tx_count`, `rx_count` | Offsets e24/e28/800/804; counts are observations, not consumed FIFO data |
| UART | `tx_pack0`, `tx_pack1`, `rx_pack0`, `rx_pack1` | Offsets 260/264/284/288 |
| UART | `tx_word`, `rx_word`, `tx_stop`, `tx_config`, `rx_config` | Offsets 268/28c/26c/25c/280 |

Log one bounded record per bank with `valid`, the sample counter/time and each
valid named field. Log skipped banks with an explicit reason such as
`uart_access_unproven`; never render missing samples as observed zeros.
The collection function stops that bank on the first failed read and clears
its previous validity bits before another attempt. It does not retry, wait,
change configuration or read a data FIFO. Snapshot fields can change while
hardware is active; do not use the report as an atomic transfer preflight.

## Bounded controller version query

`vermeer_bt_identity.h` sends only the four-byte H4 command
`01 01 10 00` for standard Read Local Version Information. It pins the byte
link callbacks/context for the transaction, supports short writes and reads,
validates the complete response, and publishes manufacturer/revision fields
only after successful validation. It does not request a Bluetooth address.

The transport owner must attest exclusive access, a known awake link, an
initially clean receive stream and an available initial command credit.
These attestations remain associated with the transaction until revoked;
they are not requests to toggle power or flow control. IBS handshakes must
already be handled by the transport. Unhandled IBS retires this query.

Each poll performs at most one write of four bytes and one read of 64 bytes.
The overall timeout is at most one second, with a 4096 poll limit even if the
clock stalls. Backwards time, late responses, transport errors, unexpected
command responses, hardware errors, malformed H4 and revoked ownership retire
the query without automatic reset or retransmission. A successful standard
version report still does not establish a vendor firmware or NVM selection.
Ownership/time are rechecked immediately after every write, including short
writes, before returning a pending result or issuing a read. Fault injection
of revocation, deadline expiry and backwards time inside the write callback
must produce zero subsequent reads and writes.

## TLMM adapter and CDC records

`vermeer_media_tlmm.h` is the candidate for the first integrated observation.
It has no UART path. Its fixed 16-field plan contains the eight Bluetooth TLMM
fields above plus the control/input pairs for camera reset GPIOs 118/120/117/119
in slot order 0/1/2/3. Select `VMT_BT_FIELDS` or `VMT_CAMERA_FIELDS`, or both.
Unknown field-mask bits are rejected before device access.

The access object declares a translated physical start, mapped byte length,
retained context, an ordered read callback, a monotonic clock callback and a
live proof callback. All five proof bits are required: exact physical model,
translated live DT resource, device mapping attributes, retained accessible
domain and retained mapping ownership. These references must remain held until
collection returns, even if permission to collect is revoked in the meantime.
The adapter preflights every requested address, then subtracts the mapping's
physical start before calling the reader. It checks all four bytes of each
register and rejects overflow or a window outside the 16 MiB TLMM resource.

The touch driver's current mapping `0x0f100000 + 0x30000` covers GPIO0..47.
It cannot be reused for GPIO81..83 or117..120. Address coverage examples are
`0x0f150000 + 0x4000` for BT and `0x0f174000 + 0x8000` for camera resets;
these are not instructions to create mappings or evidence of domain access.
The platform owner must establish each mapping and its proof separately.

Collection performs at most 16 ordered reads, with proof checks before and
after each. A failed/revoked read stops immediately and does not publish that
field. `vmt_encode` produces a single bounded `VMT1` line for the existing
console ring. `sequence` must increase within the boot; start/end use monotonic
microseconds. Status codes 0..6 mean ok, gated, bounds, read failed, revoked,
clock failed and bad argument. `fail` is the field where collection stopped,
or `ffffffff` when no individual field failed. `uart=skipped` is fixed.
The validity mask distinguishes a measured `00000000` from a missing
`--------`; unavailable timestamps use 16 dashes.

The `data` array follows `vmt_names`: eight BT fields, then camera0 config/input,
camera1 config/input, camera2 config/input and camera3 config/input. The record
also includes the requested mask and last proof mask. A partial record remains
useful evidence but is not a complete observation. Keep its source/build/boot
manifest with the CDC capture; a sequence identifies a sample within one boot.

`tools/media_records.py capture.log output.json` parses the retained CDC trace.
It rejects truncation, inconsistent masks, invalid failure indices, incomplete
success, changed records reusing a sequence and backwards sequence order.
Identical console replays are counted and deduplicated. UART remains skipped,
and hardware identity remains unverified in the decoded report.

For a reviewed integration, the platform owner should:

1. Stage the four media headers unchanged into `bsd/dev` through a dedicated
   GUI-only source patch: `vermeer_bt_h4.h`, `vermeer_bt_identity.h`,
   `vermeer_bt_observe.h`, `vermeer_media_tlmm.h`. Leave the identity query
   uncalled until an awake owned byte link exists.
2. Include `vermeer_media_tlmm.h` in the existing serialized diagnostic worker,
   or add a separate wrapper source to `bsd/conf/files`. The wrapper's read
   callback uses the retained mapping plus the supplied window-relative offset
   and the existing ordered device-read barrier. It must not map registers,
   enable a domain or update pins as a side effect of a query.
3. Supply the access object from the actual mapping/domain owner. If no such
   proof exists, collect a gated record with no device reads. Keep a permanent
   per-boot sequence, refuse increment at `UINT64_MAX`, call `vmt_capture` once,
   then print the bounded `vmt_encode` output verbatim into the console ring.
4. Queue one observation through the existing CDC diagnostic worker after
   review. Do not read MMIO in the USB transfer loop or add an autonomous poll.
   This lane supplies the candidate, tests and source paths; it does not edit
   the USB command handler or shared profile generator.

## Camera identity and the minimum observation

The retained FDT supplies four generic sensor/EEPROM slots, not verified
sensor names or chip ID tuples. Read-only inspection of the retained ext4
persist directory found AFC/DCAL/checksum files under camera and an RSSI
adjustment file under Bluetooth. These filenames do not identify fitted
sensors or Bluetooth silicon. Calibration contents were not interpreted or
modified, and the filesystem journal was not replayed.

For a minimal capture, request the already accessible TLMM control/input
values below. Each slot needs two reads of its reset GPIO; MCLK configuration
is optional if available from an existing pin snapshot. Report raw values,
not inferred sensor identities or successful power/reset sequencing.

| Slot | CCI controller and master | CSIPHY index | Reset GPIO | Optional MCLK GPIO |
| --- | --- | --- | --- | --- |
| 0 | CCI0/master0 | 5 | 118 | 102 |
| 1 | CCI2/master0 | 4 | 120 | 100 |
| 2 | CCI0/master1 | 2 | 117 | 101 |
| 3 | CCI1/master0 | 3 | 119 | 103 |

The saved TLMM base is `0xf000000`; GPIO control/input offsets are
`0x100000 + gpio*0x1000` and the following word. For reset GPIOs 117..120 this is
0x175000/0x175004, 0x176000/0x176004, 0x177000/0x177004 and
0x178000/0x178004, respectively.
Use only the already verified TLMM mapping. Log existing owned rail/clock
state if the power owner has it; otherwise report it as unknown. Do not probe
CCI/ISP or replay power sequences to fill missing fields.

A physical sensor ID read requires a powered sensor and a CCI transaction;
that cannot be obtained through a purely passive snapshot of an unpowered
camera. The next acceptable identity evidence is either an existing successful
Android sensor-probe record with slot/raw ID, or a later explicitly integrated
power/CCI transaction using verified board and sensor requirements. A library
filename, generic slot, calibration filename or model specification is not
enough to select a sensor driver.

## Validation and integration status

`CC=clang-14 sh tests/run-media-tests.sh` runs H4, identity and TLMM adapter
tests under ASan/UBSan plus nine FDT and five CDC record tests. Identity coverage includes all
write chunk sizes 1..4/read chunk sizes 1..64, absent responses, deadlines during
I/O, backwards/stalled clocks, invalid responses, reentry, revoked ownership,
duplicate responses and every UART snapshot read failure. TLMM tests reject
the touch-only window, wrong physical model, UART mappings, unaligned and
short/overflowing windows; the exact final four-byte boundary is accepted.
Every read failure and mid-read revocation is injected, with no later read.
Record tests decode the actual C encoder, check measured zero versus missing,
and reject broken/changed replays. The media workflow tracks these headers
and tests independently of the kernel workflow.

Kernel registration, a UART byte adapter, power sequencing, firmware startup,
the iOS Bluetooth interface, sensor identification and camera capture remain
unimplemented. Do not boot a separate media image to obtain these snapshots;
merge only approved observations into the device owner's next CDC session.
