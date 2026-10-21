# K70 physical touch candidate

This is an experimental native XNU input path, not a Linux module loader.
The current DMA candidate has not been booted. GUI011 remains the hardware
baseline: clocks pass, FIFO is disabled and input reports remain zero.
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

When FIFO is disabled it can select internal DMA, after checking the existing
SID0xa3 identity route and retaining two exclusive16KB pages in tested RAM.
It refuses an already enabled internal/GPI DMA mode. There are no SMMU changes,
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

`vermeer_geni_dma.h` adds a separately tested internal-DMA transfer core. It
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

The core is now called by `vermeer_touch.c` and both new headers are packaged
only into the GUI profile. `vermeer_touch_route.h` accepts only the observed
apps-SMMU geometry, one SID0xa3 match, and either existing direct bypass or a
bounded context with stage1 disabled/stage2 bypassed. It never creates a stream
mapping, disables translation or changes a context bank. The platform rechecks
that route, reset pad and the retained pages before each DMA transaction.
Cache operations run on a pinned CPU and cover whole dedicated pages with PoC
barriers. Faulted DMA retains clocks/pins/pages even if the command bit is idle.

USB `input` reads retained atomic snapshots of SMMU layout/route/refusal reason,
reset24/IRQ25 configuration and IO, inherited DMA mode/lengths, DMA status and
quarantine state. It performs no MMIO in the USB callback. Route reasons are
0 accepted,1 unknown layout,2 missing/ambiguous match,3 invalid context bank,
4 active/unsupported translation,5 not sampled. A rejected route and reset state are both
sampled before any SE/pin setup, so the next boot can distinguish their causes.

This is a conditional communication candidate, not proof of working touch or
completed board power bring-up. **Reset high does not verify AVDD/IOVDD voltage
or regulator ownership.** The candidate attempts to reuse the inherited rail
state; it does not power up a controller left in reset or upload firmware.
GUI011's reset IO sample0 means it is expected to stop and retain diagnostic
evidence if that state recurs. Actual route, rail readiness, electrical SPI and
GT9916 identity remain hardware tests; HID publication still requires validated
identity, IC information and complete packets.

Host ASan/UBSan route tests cover all105 match slots, overlapping matches,
unknown layouts, all70 valid/186 invalid context-bank indices, translation
refusal,4096 GPIO configurations and high-RAM allocation boundaries. The
generated0067 patch strictly applies to a pre-hook fixture derived from the
actual GUI011 source; all seven packaged source/header files match byte-for-byte.
Four affected units pass actual ARM64/16KB XNU syntax compilation. The independent
`gpu-touch-checks.yml` runs the GPU/touch suite and GUI isolation automatically;
full kernel linking and a new integrated-image hardware test remain pending.

Protocol/register references (no binary firmware included):

- [MiCode GENI SPI](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/53ef33eacdc4f7ac737451e4cfc91963d59e3956/drivers/spi/spi-geni-qcom.c),
  [Kalama clocks](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/53ef33eacdc4f7ac737451e4cfc91963d59e3956/drivers/clk/qcom/gcc-kalama.c),
  [Kalama pinctrl](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/53ef33eacdc4f7ac737451e4cfc91963d59e3956/drivers/pinctrl/qcom/pinctrl-kalama.c).
- [Exact-compatible Xiaomi9916 reference](https://github.com/Air-Alex/kernel_xiaomi_sm8550-modules/tree/d92dbc093c08af885d8a808a170a715c1c024312/qcom/opensource/touch-drivers/goodix_berlin_driver_9916).
- [UniXNU SM8550 touch board](https://github.com/UniXNU/UniXNU-sheng/blob/7562a779ebdbdedc17c4c7274ba993acbbec8188/package/TouchBoard.h)
  and [GENI SPI](https://github.com/UniXNU/NativeGeniSpi/tree/b5399b5646c4d7415deec6d887fd51715a97a024).
