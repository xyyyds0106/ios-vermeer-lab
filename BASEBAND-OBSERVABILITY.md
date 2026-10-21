# Observable cellular diagnostics

## Status and device ownership

Latest user steering permits manual device operation again and cancels automatic
return-to-Fastboot as a prerequisite. GPU/touch owns all phone I/O (serial, ADB,
Fastboot and reboot) and is testing the reviewed GUI012 image first. That image
has no baseband observer commands. Do not send new commands to it or start a
separate probe. The already-written offline handshake candidate below is tested
and retained for later integration; no further unattended-loop work is required.

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
5. The device owner saves the terminal result or incomplete trace and arranges
   recovery/return, including manual operation under the latest authorization.
   A software bootloader reset is optional and needs its own validation. Observe
   this same handset enumerating in Fastboot before reporting it ready.
   Missing enumeration is failure/unknown, not a cue to retry register writes.

The metadata reader retains the013 directory/ownership/range checks. MMIO
mapping and fault behavior under XNU require separate integration work; passing
the offline parser does not authorize arbitrary physical reads. Exception and
reset paths were prerequisites for unattended testing, not implemented here;
an automatic reset is no longer required for the next manually assisted test.

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

## Dry-run producer and durable acknowledgement candidate

`vermeer_baseband_observer.h` now implements a single-owner worker state machine,
not just a codec. It deliberately has **no reader callback**, physical-pointer
dereference, reset call or hardware-start entry. Its five simulated steps use
fixed fixture addresses/lengths and do not inspect memory at those addresses.
The existing USB implementation and `tools/capture-debug.ps1` are unchanged.
This candidate is separate from the coordinator's exact42ce3a7 kernel candidate;
it has not been linked, pushed or device-tested.

Synthetic records use CRC-protected binary magic `VBDR`, version1, and text
prefix `VBDR1 `. The remaining fields reuse the128-byte record layout above.
The real-record decoder rejects that binary magic and ignores that text prefix.
The dry receiver reports `synthetic_pass` separately and always sets
`metadata_pass=false` and `modem_operation_proven=false`. A transport rehearsal
cannot be mistaken for SMEM access, even when it finishes successfully.

| Interface | Caller contract |
| --- | --- |
| `vbb_observer_start(context, run, image, now_ms, timeout_ms)` | Once on a zeroed context, verified nonzero identities, monotonic time; timeout1..60000ms |
| `vbb_observer_poll(context, now_ms, publish, arg)` | Worker only; bounded polling while waiting, independent of incoming ACKs |
| `publish(arg, line, length)` | Copy all264 bytes under the shared ring lock, or copy none; return1/0, negative for fatal failure; no printf or reentry |
| `vbb_observer_ack_line(context, line, length, now_ms)` | Worker dequeues a bounded61-byte ACK copied from USB; no concurrent context mutation |
| `vbb_observer_disconnect(context)` | Permanently stops the context; no automatic retry/reuse |

The full emitted packet is a leading newline plus a263-byte record line,
264 bytes total. The leading newline separates a preceding unfinished console
fragment. Code review of retained GUI011 `serial_console.c` confirms a256-byte
per-CPU buffer, a16-byte flush buffer and `_cnputs` calls for each flush chunk.
The ARM `pe_kprintf` mirror also appends individual characters. A single printf
call therefore does not provide record-level publication. The candidate avoids
that path entirely. `vbb_observer_publish_atomic` uses try-lock, one infallible
append-while-locked, and unlock. The integration owner must bind those callbacks
inside `vermeer_debug.c` to the **same** lock/ring used by existing writers;
calling the current void/best-effort `vermeer_debug_append` does not satisfy the
success-return contract. Lock contention copies nothing and retries within the
original deadline. Ring overwrite can still lose records, so ACK remains required.

Each published BEGIN, BEFORE_READ, READ_OK and terminal record waits for
`ba TOKEN32 SEQ16 CRC8`, exactly61 characters before newline, fitting the current
63-character command limit. Token, sequence and CRC must match the one pending
frame. Early, duplicate, stale or malformed ACKs cannot advance it. Successful
BEFORE_READ acknowledgement increments only the synthetic-step counter; there
is no hardware operation to invoke. FINISHED is DONE only after its ACK.
Worker registration remains integration work. The bounded start-handshake and
queue below are now implemented as a separate, offline-tested component.

`vermeer_baseband_commands.h` implements these commands; they are **not yet
recognized by the current USB parser**. The fragment order is now explicitly
`HASH16 TOKEN32`, matching the coordinator's latest interface request and the
host builder; the previous documentation-only `TOKEN32 HASH16` form is rejected.
USB parses/copies complete commands into an8-entry bounded value queue. The sole
worker owns staging and returns START/ACK/STOP requests without invoking observer,
MMIO, file or reset functions inside this header:

| Proposed command | Length without newline | Staging action |
| --- | --- | --- |
| `bb TOKEN32` | 35 | Start a new nonzero128-bit token, clear32 hash bytes/4-bit mask, set fixed2s deadline |
| `bh0 HASH16 TOKEN32` through `bh3 HASH16 TOKEN32` | 53 | Supply exactly8 bytes of the SHA256 at offsets0/8/16/24, strictly in0..3 order; token must match |
| `bg TOKEN32` | 35 | Commit only if token matches, all4 chunks exist, digest is nonzero and deadline has not expired; call `vbb_observer_start` once |
| `ba TOKEN32 SEQ16 CRC8` | 61 | Existing candidate ACK parser, worker only; no staging changes |

The parser requires lowercase hex of exact width, exact spaces and no trailing text. Every
hash fragment carries the entire run token, avoiding short-ticket ambiguity.
Duplicate/out-of-order/conflicting fragments, wrong tokens, malformed commands,
expiry and disconnect latch failure and erase partial staging. Queue overflow
also cancels all queued events, including an unclaimed GO. No fragment extends
the deadline: the2s window begins at BEGIN's USB receive timestamp and includes
queue residence. Worker time must be monotonic and not earlier than event time.
A new `bb` is rejected during staging, running or after use, for the same token
or a different one. There is no reset/reuse API; initialize once per device boot,
not per reconnect. A complete nonzero hash and correct GO produce exactly one
immutable value-copy START request. Errors before that point produce0 starts.

The caller must hold one common lock for **every** queue/staging operation:
USB enqueue, overflow/disconnect fault, worker-next and worker acceptance of START
into an idle observer. The worker alone touches staging/observer. `worker_next`
must run periodically even with an empty queue so silence still expires. Hold
that lock until the pure `vbb_observer_start` has accepted the copied request;
never defer an unguarded START beyond unlock. Release before publishing/polling,
and process queued faults before the next poll/ACK. This provides a defined
linearization point: a disconnect before START acceptance cancels it; a later
disconnect stops future work but cannot retroactively undo an accepted start.
These headers do not contain atomics or acquire a platform lock themselves.
The integration owner must bind the common lock correctly, not treat the queue
as lock-free SPSC. Upstream USB line-overflow must report a sticky fault, not
truncate into a valid command prefix. Other console commands must not be routed
to this strict baseband parser.

Interfaces are `vbb_command_enqueue`, `vbb_command_fault` and
`vbb_command_worker_next`; START and ACK requests carry copied identity/fields.
STOP makes the integration worker call observer-disconnect, not reset the phone.
`tools/baseband_start_commands.py` builds an immutable tuple of six byte strings
without opening a device or file. The additional C ASan/UBSan test covers100,000
parser mutations, every truncation, immutable input/request copies, queue
saturation, lost/deferred commit, clock/timeout/disconnect and single-use state.
Five Python suites send the actual builder output to a local C harness which
passes START and queued ACKs into the existing observer: normal completion is
5 synthetic steps, malformed/pre-commit failure cases are0 starts/0 steps.
Existing6 trace and10 durable observer suites and all3 AArch64 header checks pass.

An ACK timeout after a published BEFORE_READ emits REFUSED/result3 at exactly
that phase, without READ_OK or ABORT. The trace validator now accepts this precise
sequence; it rejects a different phase or refusal code. If BEGIN could never be
published, it stops with no invented trace. If a simulated step completed but
READ_OK could not be published, it stops with an incomplete trace rather than
claiming the step was never performed. Terminal ACK timeout, disconnect, backward
clock or deadline overflow stops permanently. No later poll resumes the job.

`tools/baseband_observer.py` is a host library with no serial/ADB/Fastboot imports.
The integration owner passes received byte fragments to `DryReceiver.feed` and
sends only returned ACK bytes. It handles every split/one-byte delivery, bounded
lines, CRC, identities and ordered transitions. For each accepted frame it
performs a complete journal write, flush and `os.fsync` **before returning ACK**.
Short write, write/flush/fsync error or malformed record permanently poisons the
receiver and returns no ACK for that frame. `create_journal` exclusively creates
a host file and cannot overwrite an earlier run; POSIX also fsyncs its directory.
The owner provisions the Windows directory. ACK trusts successful host storage
flush semantics; it is not cryptographic proof against a dishonest host or a
disk that ignores flush requests. Partial journal writes remain failure evidence,
and are not silently resumed as a new run.

The extended runner passes the original codec suite plus100,000 malformed ACKs,
publication contention/failure, timeout at the deadline, duplicate/early/wrong
token/sequence/CRC, clock/disconnect termination and a real-ring test with1000
records concurrently interleaved with another thread's console fragments.
All1000 records remain whole across ring wrap and1..127-byte extraction.
Ten Python suites include all265 split positions of the264-byte packet, one-byte
reassembly, interleaving/corruption rejection and durable-I/O failure injection.
End-to-end tests run the actual C worker as a local child process, feed its output
bytewise into the receiver, write/fsync a real temporary host file and return
the generated ACK through the child's stdin. Success ends with5 synthetic steps;
persisted-but-lost ACK, wrong ACK, disk-flush failure and disconnect end with0
steps. Both portable headers pass freestanding AArch64 syntax checks.

Before actual SMEM reads, the integration owner still must attach the tested
staging/queue to its platform lock and worker, the atomic ring adapter and capture
ACK plumbing, then perform device-side dry-run validation. Exception isolation
remains required; manual recovery can be arranged for the next test. Do not add a reader callback
to bypass those prerequisites or include this code in an already-frozen image.

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
