# K70 active media transport candidates

This lane adds isolated code under `xnu/vermeer/media/`. Nothing is staged in
the build profile, USB command router, boot hooks or an image. A host test is
not a working camera or Bluetooth controller.

## Camera CCI register reads

`vermeer_camera_cci.h` implements an active hardware transaction, using the
fixed Qualcomm CCI register and command layout from sm8550-modules commit
`92a9fde479d3262da080cfd28bab0775b0da500a`. The private verified source manifest
is `host-tools/upstream-camera-bluetooth-functional/source.json`. The original
implementation requires no Linux subsystem and introduces no Apple ABI.

The platform creates one `struct vcci` with `vcci_init`, retaining a device
mapping for exactly one 4 KiB CCI controller and an exclusive lease excluding
both masters, both queues and the IRQ handler. `vcci_io` supplies ordered
32-bit reads/writes, a monotonic clock and bounded pause. The platform must
qualify the actual hardware version/layout and configure CCI timing and the
fitted sensor's rails/reset/MCLK before asserting `VCCI_CONFIG`. These
callbacks do not perform those missing operations.

`vcci_read_register` accepts a verified slot, seven-bit sensor address,
one/two-byte register address and 1..16-byte read. Slot routing comes from
the retained K70 FDT: 0=CCI0/master0, 1=CCI2/master0, 2=CCI0/master1,
3=CCI1/master0; CCI0/1/2 bases are ac15000/ac16000/ac17000. This is routing
evidence, not sensor identity. No sensor address or chip ID is guessed.

The transaction checks the qualified HW version, all queues and receive
FIFOs, and rejects preexisting IRQ status. Queue1 receives SET_PARAM with
zero retries, LOCK, address WRITE_DISABLE_P, READ, UNLOCK and REPORT. It
requires both RD_DONE and the post-UNLOCK report, an empty queue, exact FIFO
word count and final empty FIFO before returning data. The first FIFO byte
is retained as raw metadata, not treated as a sensor ID. Only its completion
bits are acknowledged. No reset/halt, timing, rails or GPIO writes occur.

Every I/O callback is surrounded by live proof/time checks. The budget is
20 ms and 1000 polls, including stalled clocks. Errors leave output
unchanged and permanently retire the object. Any touched/started failed
transaction must retain platform resources until qualified recovery; the
module does not retry, reset, release a potentially locked master or claim
that a failed write had no effect. Synchronous callbacks themselves must
be bounded and hardware fault-contained by the provider.

Offline validation: `CC=clang-14 sh tests/run-camera-bluetooth-functional-tests.sh`.
The CCI suite exercises all four slots and read lengths 1..16, with every
MMIO callback failed, revoked, delayed to deadline and reentered. These
tests establish software behavior only. CSI/CSIPHY, sensor streaming, ISP,
DMA/SMMU and the iOS camera service interface are still absent.

## Bluetooth UART GPI datapath

`vermeer_bt_uart_gpi.h` is a real byte adapter for an already configured and
exclusively leased UART/GPI pair. It writes UART CONFIG/GO/DMA descriptors,
performs synchronous cache maintenance, rings hardware doorbells, validates
completion pointer/type/status/length, copies RX bytes and rearms RX. It is
independent of the touch SPI/GPI implementation and does not use internal SE
DMA for a FIFO-disabled UART.

The retained FDT identifies UART SE6 at `898000+4000`, GPI at `800000+60000`,
SID `436`, dynamic GPII mask `3e`, static mask `1`, and event factor2. It does
**not** contain a `dmas` property for this UART. `VBUG_ROUTE` therefore requires
the platform's qualified live SE6/GPII binding; mask membership alone cannot
supply this proof. GPII1..5 are accepted only after that proof. The callback
offsets are relative to the mapped GPI top: for instance I, channel0 context
is `10000+4000*I`, not `20000+4000*I` (the pinned vendor driver subtracts the
DT EE offset10000). UART callbacks use bank1; GPI uses bank0.

`vbug_attach` takes five distinct permanently owned CPU16KB pages and their
actual SID436 IOVAs: TX, RX, TX ring, RX ring, event ring. It rejects zero,
unaligned, aliased, overflowing or >=40-bit addresses. It verifies UART
firmware protocol2 and FIFO-disabled mode; both started channels' geometry,
empty pointers and protocol2/SE6 scratch; event context geometry; and GPI
errors. Rings contain64 TREs; event factor2 gives256 events. The provider
must establish these contexts, masks, clocks, pins and mappings beforehand.
Attach performs no allocation, takeover, power/reset, firmware upload or
baud change. Provider callbacks must be serialized, bounded and fault-safe.

`vbug_write` accepts up to4096 bytes once into DMA; another TX returns
would-block until its matching EOT completes. `vbug_read` and `vbug_poll`
consume bounded events, accept variable-length RX EOT/EOB, preserve received
bytes, and rearm the continuously running secondary UART. TX is limited to
100ms/4096 polls, even with a stalled clock. Error, malformed/duplicate event,
overflow, lease loss or backwards time retires the stream permanently.
RX remains armed after a successful query: **all pages, IOVA mappings and
domains remain retained until platform-qualified channel stop/recovery**.

`vbug_identity_link` connects this hardware adapter to the existing standard
Read Local Version state machine. The owner explicitly attests initial clean
RX and a command credit after firmware/IBS initialization. No MAC-address,
vendor-command, discovery or pairing transaction is issued by that helper.
Tests traverse actual DMA pages/TREs/event-ring logic for every split of the
version response, five GPII instances and300 ring-wrap exchanges; every
completion callback fault/revocation/deadline/reentry boundary is injected.

## Native iOS16 UART service candidate

`vermeer_bt_uart_service.c/.h` registers a privileged datagram kernel control
named `com.apple.uart.k70.bluetooth`. It implements the actual non-Skywalk
BM3UART socket contract found in the retained official iPhoneX iOS16.7.16
20H392 `bluetoothd`, SHA256
`8b7c0a0bea51544db82269d7f2775ab7996b8740927283428c4753e032df3a8f`.

The offline disassembly establishes:

* `100066bdc` takes a configurable UART endpoint parameter; it tokenizes with
  a comma, then parses the baud with `strtoul`. The default endpoint is
  `com.apple.uart.sk.bluetooth`. Socket creation is PF_SYSTEM32,
  SOCK_DGRAM1, SYSPROTO_CONTROL2, with CTLIOCGINFO and connect.
* That exact default name takes a mandatory Skywalk setup branch: set
  `800c5414` (12-byte channel spec), get `40105412` (16-byte channel UUID),
  then `os_channel_create`. Failures close the socket; there is no ordinary
  socket fallback on those failures. This candidate does not register that
  name or fabricate a Skywalk UUID.
* A different endpoint name takes the ordinary socket path, using TIOCGETA
  `40487413` and TIOCSETA `80487414`, each72-byte ARM64 termios. Existing
  write/read fallback paths operate on that socket. The service accepts raw
  8N1 at the platform's already configured baud, and rejects unsupported
  baud, parity, stop-bit or software-transform requests. It performs no
  firmware baud-change command.

`vermeer_bt_uart_service_start(transport, baud)` transfers permanent exclusive
ownership of the attached GPI adapter to one polling worker. No other caller
may access it afterward, including the identity query. Native send callbacks
copy complete mbuf records to an8192-byte TX queue; only the worker touches
hardware. RX backpressure retains a permanent spool instead of dropping
bytes. No service mutex or socket lock is held during device access or
`ctl_enqueuedata`, avoiding the socket-lock/service-lock inversion. Start
wraps every provider callback and adds a live proof; disconnect or service
failure revokes the hardware path at its next callback boundary and refuses
reconnect. There is no automatic reset or release of active DMA resources.

The native source passes the actual retained K70 ARM64/16KB XNU recipe,
including static assertions of termios size and both option values. The host
suite compiles its unchanged callback/worker logic against explicit XNU API
mocks and checks mbuf failures, queue pressure, termios rejection, DMA/socket
flow, retained RX on ENOBUFS, disconnect during MMIO, and no later device I/O.
Mock declarations are not evidence of ABI compatibility; the native compile
checks that separately. Neither check establishes linkage or hardware success.

## Exact integration work remaining

No shared build/profile/hooks or root filesystem files were changed. Stage
the `media/` directory with its relative parent dependencies
`vermeer_bt_identity.h`/`vermeer_bt_h4.h`; compile
`media/vermeer_bt_uart_service.c` only in the reviewed K70 profile. Platform
owners must first implement/qualify retained resources and callbacks, then
call `vcci_init` or `vbug_attach` with those actual resources. Native service
start is a separate explicit action after the adapter is ready.

The next Bluetooth integration must select the non-Skywalk UART parameter
`com.apple.uart.k70.bluetooth,BAUD` in the daemon's existing transport
configuration. The function parameter format is verified; its factory/table
selection and configuration source are **not yet established**. Startup also
checks `/dev/cu.bluetooth` or `/dev/uart.bluetooth`, then I/O services
`marconi-bt`/`bluetooth`. This candidate creates no placeholder nodes or
services merely to bypass that check. A real startup integration or verified
native serial endpoint is still required. Neither socket registration nor a
successful HCI identity makes Bluetooth discovery/pairing work by itself.

The H10ISP binary contains `AppleH10CamIn` and `AppleH10CameraInterface` and
imports IOServiceOpen plus scalar/struct/async IOConnect methods. Exact
selectors, request structures, firmware messages, frame-buffer ownership and
asynchronous capture ABI remain unknown. No Apple-class stub or fake frame
source is registered. The CCI transaction does not implement Apple's ISP ABI.

Private handoff and exact file hashes live in
`device-audit/camera-bluetooth-functional-handoff.json`. Private primary-source
manifests, ABI snippets, native compiler diagnostics and host logs accompany
it. No phone I/O, boot, reset, partition write, data transmission, push or
firmware upload occurred in this lane.
