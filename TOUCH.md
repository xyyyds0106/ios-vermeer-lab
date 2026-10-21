# K70 physical touch candidate

This is an experimental native XNU input path, not a Linux module loader.
GUI012/source42ce3a7 has been booted: the user confirms Hello/setup, display
submission continues, USB reopen/manual sampling passes, and touch remains0.
It stops before any touch transaction with inherited DMA mode1, reset cfg1/io0,
and no matching wrapper SIDa3. That does not establish GPI activity or rail state.
**The next source corrects the transport design: FIFO-disabled K70 SPI requires
GPI/GSI, not internal DMA. GPI is not implemented and is explicitly refused.**
The existing Hello milestone did not include
physical input: GUI009 records zero touch calls and HID dispatches after the
reported swipe, with Setup/SpringBoard still in their main event loops.

The GUI profile alone includes `vermeer_touch.c`. `vermeer_touch=1` and the exact
`physical-model=Redmi K70` ADT property are required. The enabled K70 firmware DT
selects `xiaomi,vermeer-spi` on GENI SE4 at0xa90000, CS0, with GPIO44..47 for
qup1_se4_l0..3. GPIO24 is reset and GPIO25 interrupt. Clock101 is
GCC_QUPV3_WRAP1_S4_CLK: vote bit26 at GCC0x52008, branch0x184e4. Wrapper1 AHB
uses vote bits20/21, branches0x2327c/0x23280. Wrapper core2x/core additionally
use bits18/19, branches0x23294/0x23284, matching the UniXNU SM8550 board path.
TLMM register addressing includes
the Kalama0x100000 offset. The disabled I2C alternative is not used.

The worker waits for a permanently retained HID service, votes only those
five clocks, and checks core/SE4 readiness before accessing the sequencer. Following
MiCode's clock driver, an AHB clock in hardware-gated mode may report off while
idle; its vote permits bus accesses to wake it. A parked SE4 root can be force
requested without changing its rate, parent, DFS table or PLL. The additional
root-enable bit is restored on failure. Clock snapshots are retained for USB
queries without performing MMIO in the query callback.
It requires SPI firmware,32-bit FIFOs, an idle engine and the reset pad already
high in GPIO mode (with a high output latch as well if output-enabled).
Only the four observed SPI pins are configured; reset/interrupt GPIOs and PMIC
rails are not changed. Existing GCC root/DFS/PLL settings are retained; a large
internal serial divider is used for the initial transport.

The FIFO path is entered only with FIFO enabled, DMA/GSI disabled and an idle
SPI engine. FIFO-disabled hardware is observed and refused as GPI-required.
There are no SMMU changes,
PMIC/reset writes, firmware/configuration upload or storage access.
Version PID9916 and checksums must match, followed by bounded IC-information
tables with8-byte header/point records and valid native axes. Android's
143999/319999 coordinates are virtual precision, not wire coordinates. Valid
native positions map to the1440x3200 physical panel; existing HID translation
maps that panel to the centered1125x2436 iOS canvas.

Header, contact count, unique finger IDs, type, coordinates and payload checksum
are validated before publishing any part of a frame. Removed contacts release
first. Duplicate stationary frames do not generate repeated events. The only
controller data write clears the verified event status after a valid report.
Unknown request/stylus formats are not treated as fingers. A bus error retires
the transport; repeated malformed reports stop it and release held contacts.
Clock/pin/register state is restored only when the engine is idle. An engine
that does not quiesce keeps its resources until reboot and is never reused.

Host tests cover malformed/truncated packets, variable IC tables, native-axis
scaling, multi-contact release,1..1032-byte FIFO transfers and tails, impossible
FIFO counts, error/timeout retirement. These checks cannot establish rail state,
firmware compatibility, electrical communication, IRQ behavior or real touch.

## DMA transport work

GUI011 hardware evidence passes clock preflight but finds FIFO disabled. It
stops before a GT9916 transaction, so the FIFO-only driver above cannot handle
that observed state.

`vermeer_geni_dma.h` retains a separately tested experimental internal-DMA core,
which is **not called by the corrected K70 driver**. Its host tests do not
establish compatibility with this board's FIFO-disabled firmware. The core
requires distinct, retained, whole CPU pages for TX/RX, a live board route/reset
authorization callback and synchronous cache-maintenance callbacks. It preserves
upper physical-address words, arms RX before TX, latches TX/RX/master completion
separately, and requires an idle command engine plus successful CS deassertion
before invalidating/copying RX. Any transfer error retires the bus and quarantines
the pages and stream mapping until reboot. Abort/idle alone never authorizes
freeing DMA memory. An authorization or initial cache failure starts no command.

`tests/run-gpu-touch-tests.sh` covers every1..1032-byte transfer for4KB/16KB
pages, high physical addresses, stale and staggered completion, missing TX/RX/
master completion, active-engine timeout, bus errors and cache failures. The
core also passes a standalone ARM64 compilation with the current XNU recipe.

Pinned official `drivers/spi/spi-msm-geni.c` matches the saved `qcom,spi-geni`
compatible. Its `select_xfer_mode` requires GSI when FIFO is disabled and its
setup requests GPI TX/RX channels. This is stronger board-source evidence than
the generic FIFO-only reference. It does not prove the loaded binary equals
that exact source. The former internal-DMA selection/allocator is removed from
the K70 driver; the experimental reusable core remains separately tested.

The enabled SPI DT resolves both DMA descriptors to `qcom,gpi-dma@a00000`,
range0xa00000+0x60000, SID0xb6. Wrapper1's SID0xa3 is a different master.
The5-cell TX/RX descriptors are `(0,4,1,64,2)` / `(1,4,1,64,2)`. Pinned
`drivers/dma/qcom/msm_gpi.c` interprets these as channel0/1, SE4, SPI protocol1,
64 requested TREs, static GPII field1 selecting **instance0**, and priority0.
The fifth cell is not an execution-environment number. The provider declares
static-gpii-mask1, GPII mask0x1e, EE register offset0x10000, and a1MiB IOVA pool
starting0x100000. Do not mix it with the separate0x800000/SID0x436 provider.

`vermeer_touch_route.h` snapshots only the observed apps-SMMU geometry and
fixed SIDa3/SIDb6 allowlist. Unknown layout or missing/ambiguous mapping is
refused. It never creates a stream mapping or changes a context. A valid
identity snapshot does not establish ownership of a GPI instance or its rings.
The next source records both routes plus GSI_EVENT_EN, SE_IRQ_EN, GENI_STATUS
and TX/RX DMA status, retaining resources for any inherited DMA/GSI state.

USB `input` reads retained atomic snapshots of SMMU layout/route/refusal reason,
reset24/IRQ25 configuration and IO, inherited mode/lengths, DMA status, GSI
events and IRQ routing. It performs no MMIO in the USB callback. Route reasons are
0 accepted,1 unknown layout,2 missing/ambiguous match,3 invalid context bank,
4 active/unsupported translation,5 not sampled. A rejected route and reset state are both
sampled before any SE/pin setup, so the next boot can distinguish their causes.

This remains an unsupported GPI path, not completed touch/power bring-up.
GPI route/reset snapshots are collected before checking FIFO depth or command
idle, so active firmware and FIFO geometry cannot hide that evidence. Cleanup
requires a positively sampled, idle SPI FIFO baseline with DMA/GSI disabled.
Unknown/unmapped engines and FIFO-disabled GPI retain clocks even when MODE_EN,
GSI_EVENT_EN and GENI_STATUS happen to be zero; idle does not confer ownership.
**Reset high does not verify AVDD/IOVDD voltage
or regulator ownership.** The candidate attempts to reuse the inherited rail
state; it does not power up a controller left in reset or upload firmware.
GUI012 reset cfg1/io0 confirms a GPIO input sampled low, without proving why.
Actual GPI route, rail readiness, electrical SPI and
GT9916 identity remain hardware tests; HID publication still requires validated
identity, IC information and complete packets.

Host ASan/UBSan route tests cover all105 match slots, overlapping matches,
unknown layouts, all70 valid/186 invalid context-bank indices, translation
refusal,4096 GPIO configurations and high-RAM allocation boundaries. The
generated0067 patch strictly applies to a pre-hook fixture derived from the
actual GUI011 source; all seven packaged source/header files match byte-for-byte.
The independent `gpu-touch-checks.yml` runs the GPU/touch suite and GUI isolation.
The new GPI snapshots and policy have local regression/ARM64 checks; another full
build/device test for this correction remains pending. GUI012 is retained as
hardware evidence of preflight refusal, not a successful internal-DMA test.

Protocol/register references (no binary firmware included):

- [MiCode downstream GENI SPI](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/53ef33eacdc4f7ac737451e4cfc91963d59e3956/drivers/spi/spi-msm-geni.c),
  [GPI channel binding](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/53ef33eacdc4f7ac737451e4cfc91963d59e3956/drivers/dma/qcom/msm_gpi.c),
  [Kalama clocks](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/53ef33eacdc4f7ac737451e4cfc91963d59e3956/drivers/clk/qcom/gcc-kalama.c),
  [Kalama pinctrl](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/53ef33eacdc4f7ac737451e4cfc91963d59e3956/drivers/pinctrl/qcom/pinctrl-kalama.c).
- [Exact-compatible Xiaomi9916 reference](https://github.com/Air-Alex/kernel_xiaomi_sm8550-modules/tree/d92dbc093c08af885d8a808a170a715c1c024312/qcom/opensource/touch-drivers/goodix_berlin_driver_9916).
- [UniXNU SM8550 touch board](https://github.com/UniXNU/UniXNU-sheng/blob/7562a779ebdbdedc17c4c7274ba993acbbec8188/package/TouchBoard.h)
  and [GENI SPI](https://github.com/UniXNU/NativeGeniSpi/tree/b5399b5646c4d7415deec6d887fd51715a97a024).
