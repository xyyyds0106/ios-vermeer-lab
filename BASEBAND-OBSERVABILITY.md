# Observable cellular diagnostics

## Status and device ownership

The user now requests automatic log collection and software return to Fastboot,
without depending on photographs or button presses. GPU/touch owns all phone
I/O (serial, ADB, Fastboot and reboot). This baseband change is offline only.
Do not start a second device client or another framebuffer-only probe.

Diagnostic013 remains **execution result unknown**. Its temporary boot command
returned success; the photograph subsequently supplied was byte-identical to
the retained diagnostic012 photograph. Neither its metadata result nor its
automatic reset was observed. Its code has no CDC/serial log export, no persistent
result record and no bootloader-destination request. A nominal 90-second PSCI
SYSTEM_RESET cannot establish arrival in Fastboot. The image cannot be given a
new remote interface after boot by opening a host serial port.

## Minimal no-partition-write approach

Prefer the existing XNU CDC transport, which already exported GUI011 logs and
bounded queries. `vermeer_usb_debug.c` exports `vermeer_debug.c`'s same-boot
1MiB console ring. It uses permanent owned DMA memory and checks the existing
USB SMMU route. The current parser accepts at most63 command characters;
commands are ping/status/replay/sample/frame/input. It has no baseband command,
result acknowledgement or bootloader reset command. Console logging is best
effort: lock contention drops bytes, wrap discards older bytes, and replay can
duplicate output. Printing a result is not proof the host received it.

The next integrated diagnostic needs this sequence, before hardware access:

1. The sole device owner verifies the image hash and opens CDC once. A new
   nonzero128-bit run token is established with a bounded handshake. The device
   must echo that token and the expected image identity. The image hash can be
   supplied after launch; it cannot be embedded as its own full-image digest.
   Echoing it correlates records with the manifest, not device attestation.
   The future handshake must fit the command limit or explicitly implement
   bounded chunk assembly; do not send an oversized command to the current code.
2. A worker publishes BEGIN, then BEFORE_READ with the exact bounded address,
   length and phase. The host validates and durably saves each record before
   acknowledging that run/sequence/CRC. No acknowledgement, timeout, wrong token,
   stale sequence or lost USB means **no read**. The callback on the USB polling
   thread only queues work/acknowledges records; it must not perform SMEM reads.
3. Successful reads publish READ_OK. Metadata validation publishes FINISHED
   with the actual parser result, or REFUSED without touching a rejected range.
   The host requires the complete ordered trace and final result before reporting
   metadata pass. This is never modem/registration/call/data success.
4. Before enabling actual reads, establish exception containment and observation
   for this XNU build. Baremetal vectors must not replace XNU's VBAR. A data abort
   must stop the job; no skip-and-continue or fault-page retry. A narrow fatal
   path may populate a preallocated ABORT record with phase/ESR/FAR/vector, but
   cannot assume printf locks, scheduling or CDC remain usable in that context.
   If the terminal record is not received, the host's persisted BEFORE_READ
   identifies the last attempted range and reports incomplete/unknown. It does
   not invent an ESR or call the event an observed hardware abort.
5. The device owner saves the terminal result or incomplete trace and only then
   requests a separately verified software bootloader reset. Observe this same
   handset enumerating in Fastboot and verify product before reporting it ready.
   Missing enumeration is failure/unknown, not a cue to retry register writes.

The metadata reader retains the013 directory/ownership/range checks. MMIO
mapping and fault behavior under XNU require separate integration work; passing
the offline parser does not authorize arbitrary physical reads. Exception and
reset paths are prerequisites for unattended testing, not implemented here.

## Implemented offline record contract

`vermeer_baseband_record.h` implements the encoder/decoder;
`tools/baseband_records.py` validates saved text logs only. Neither opens a
device, sends acknowledgements, reads SMEM nor survives reset. Records are
128 little-endian bytes, printed by a future producer as `VBBR1 ` followed by
256 hex digits and newline. Version/length, reserved bytes, CRC32/IEEE, run and
image identity, ordered sequence and state transitions are checked. CRC detects
corruption; it is neither authentication nor proof of atomic publication.

| Offset | Field |
| --- | --- |
| 0 / 4 / 6 | `VBBR` magic / uint16 version1 / uint16 length128 |
| 8 | uint64 sequence, starting at1; wrap/replay rejected |
| 16 / 32 | 16-byte run token / 32-byte image SHA256 |
| 64 / 68 | uint32 state / uint32 metadata phase0..5 |
| 72 / 80 / 84 | uint64 requested address / uint32 length / uint32 parser result |
| 88 / 96 / 104 | uint64 ESR / uint64 FAR / uint32 exception vector |
| 108 | uint32 flags: bit0 exception present, bit1 FAR valid |
| 112..123 / 124 | zero reserved bytes / CRC32 over bytes0..123 |

States:1 BEGIN,2 BEFORE_READ,3 READ_OK,4 FINISHED,5 ABORT,6 REFUSED.
ABORT retains the requested range; FAR carries the individual fault address
only if the exception handler has determined it is valid. Missing or invalid
FAR is zero with its valid flag clear, not a reported fault at address0.
Phases:1 TOC header,2 TOC entries,3 TOC recheck,4 partition header,5 recheck.
Only FINISHED/result0 after phase5 counts as metadata pass. The receiver checks
the read range sequence and excludes the known012 fault page. Partition
ownership still comes from the device parser, not the record format.

The saved-log decoder requires the launch's expected token and image SHA.
Old photographs and old logs cannot supply those values. It rejects mixed runs,
missing/duplicate/out-of-order frames, bad CRC/reserved fields, unknown versions,
truncated frames and premature success. A valid prefix without a terminal frame
is incomplete; an ordinary013 log with no records remains unknown.

Validation: `sh tests/run-baseband-record-tests.sh` runs C ASan/UBSan with
100,000 mutations, every single-bit corruption and truncation, exception/state
constraints, six Python trace suites, C-to-Python interoperability and a
freestanding AArch64 include check. Test records are synthetic, not phone data.
No linked kernel producer or new boot image is delivered by this change.

## Reset and cross-reset RAM evidence

Saved handset FDT (`device-audit/baseband-observability-audit.json`, private)
contains an IMEM restart_reason child at offset0x65c under0x146aa000. It also
contains `/soc/reboot_reason` with compatible `qcom,reboot-reason`, referencing
the PMK8550 SPMI SDAM `restart@48` cell, size1, bits1..7. A node describes a
resource; it does not prove which boot firmware policy consumes it or establish
a working write/reset sequence. The older SWEET shim writes an IMEM bootloader
magic and PS_HOLD, but is not K70 validation and must not be copied as such.

The published Qualcomm
[reboot-reason driver](https://android.googlesource.com/kernel/msm.git/+/9671bc00773d7f73172f1cf38a7cd638091d0214/drivers/power/reset/qcom-reboot-reason.c)
maps `bootloader` to a reason byte and writes the named NVMEM cell. This is
supporting source evidence for a route to investigate, not a verified K70
implementation or permission to write any PMIC address. This particular source
is not the locally pinned MiCode source. GPIO, PMIC, IMEM, download-mode and
storage writes are absent from this change. The device owner must determine
and verify the actual reset path and report the terminal device state.

Cross-reset RAM is a fallback only after establishing an exclusively owned
reserved range, firmware retention, cache publication and a bounded importer
in the following image. None is established here. Probe013's private BSS can
be overwritten by the next loader; the same-boot console ring is not persistent.
Do not invent a scratch physical address or reuse SMEM, framebuffer, modem
carve-outs, NV, calibration or a storage partition for result retention.
