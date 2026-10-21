# K70 cellular bring-up

Scope agreed on 2026-10-21: cellular calls, packet data and domestic roaming,
targeting China Mobile, China Unicom, China Telecom and China Broadnet. No
carrier has passed a test yet. Camera, general-purpose buttons and Bluetooth
are separate work; call audio and IMS remain dependencies of usable voice calls.

## Current result

This is a protocol/inventory foundation and a prepared read-only hardware
diagnostic, not an operational modem driver. No SIM, registration, data session,
roaming transition, voice call or IMS registration is demonstrated on this XNU
build. No cellular service is advertised to iOS. The existing GUI kernel does
not compile these headers into a live modem transport.

- `tools/baseband_inventory.py` reads a saved device tree, follows hardware
  phandles and emits a bounded inventory without root serial-number properties.
  It preserves parent-bus addressing and ranges rather than silently assuming
  every `reg` value is a CPU physical address. This handset resolves five roots
  and 25 dependency nodes. Node presence and DT status are not runtime evidence.
- `xnu/vermeer/vermeer_baseband_protocol.h` decodes bounded QRTR v1/v3 framing
  (v3 optional headers are unsupported), server announcements, and complete QMI
  messages with duplicate/truncated TLV rejection. Transaction/message matching
  preserves modem error results. Four read-only query builders cover DMS
  operating mode, NAS serving system, UIM card status and WDS packet status.
  There is no transport, service registry, restart handling, flow control,
  network attachment, dialing or settings writer in this module.
- `xnu/vermeer/vermeer_baseband_smem.h` decodes a private SMEM snapshot for the
  APSS/MPSS partition and uncached GLINK descriptor/TX/RX items. Invalid lengths,
  overlaps, ambiguous partitions and duplicate requested items fail. Cached
  allocations are explicitly reported as unsupported, not absent. Capturing a
  snapshot is best effort; it can contain stale or concurrently changed state.

The wire cap is 64 KiB; the bring-up decoder is intentionally a supported
protocol subset, not a replacement for Linux QRTR or a full QMI implementation.
Future callers must match discovered source/destination endpoints and service
instance, serialize access, enforce timeouts, discard stale transactions on
remote restart and honor QRTR `confirm_rx` before publishing modem state.

## Saved hardware contract

| Component | Observed configuration |
| --- | --- |
| MPSS remote processor | `qcom,kalama-modem-pas`, register window `0x04080000` |
| SMEM | reserved range `0x81d00000` + `0x200000` |
| MPSS memory | `reg` is `0x8a800000` + `0x10800000`; the node name contains a different address and must not be used as the address |
| Modem DTB memory | `0x9b000000` + `0x80000` |
| GLINK | remote SMEM host 1, label `mpss`, channel `IPCRTR`, IPCC mailbox arguments 2/0 |
| SMP2P | APSS 0 / MPSS 1; SMEM item IDs 435/428; IPCC mailbox arguments 2/2 |
| IPA/GSI | parent-bus bases `0x03e00000` / `0x03e04000` |
| IPA application SMMU | application SMMU at `0x15000000`, DT stream argument `0x4a0`; runtime ownership unverified |

These facts come from the retained handset FDT, whose SHA256 is
`7cb9d42f7a30e63778fe8d693fb4f20e9d93c05da45962581e1d61e1e7f2d2d8`.
The private decoded inventory is `device-audit/k70-baseband-contract.json` in
the workspace parent. Do not include the raw handset tree or backups in Git.

Pinned MiCode source configures `modem.mdt`, `modem_dtb.mdt`, PAS IDs 4/0x26,
DSM setup, below-32-bit DMA and `auto_boot=false`. This describes the Linux
driver, not the live bootloader state. Starting MPSS under XNU needs an audited
firmware/authentication, memory ownership, clock/power and restart lifecycle.
SMEM host 1 is not evidence that QRTR node 1 hosts every service.

## iOS compatibility finding

Eight complete recorded GUI011 CommCenter crash stacks were symbolicated
offline. Some reach `ApplePDPHelperInterface::callStructMethod` then
`std::mutex::lock` / `pthread_mutex_lock` with the logged fault address zero.
Others show C++ termination without the originating exception in this trace.
This identifies an additional Apple data-interface integration problem; it
does not prove a particular missing IOKit selector, nor that suppressing the
crash would produce a modem. The local report is
`device-audit/commcenter-011-crash-symbols.txt`. No Apple binary was modified.

## Prepared hardware diagnostic

The sibling local `k70-probe` directory contains `baseband_state.c`,
`baseband_entry.S`, `baseband_guard.h`, `tests/baseband_guard_test.c` and
`build_baseband.sh`. The build reuses its existing exception vectors, linker
script and Android v4 packager. Its enlarged BSS uses page-relative entry
references for the private 2 MiB snapshot.

`k70-probe-012-baseband.img` first checks EL1, disabled MMU/data cache,
page-aligned image placement and the live FDT's exact SMEM range, phandle,
consumer and modem-compatible properties. It copies SMEM into its own image
memory and displays parser status, partition/item sizes and retained GLINK
head/tail values. An access exception stops the probe and displays diagnostics.
The only intended writes are its own image/stack and the known framebuffer;
PSCI resets the device after 90 seconds. It performs no modem register writes,
mailbox traffic, QMI commands, firmware loading or storage operations.

The snapshot cannot demonstrate a live modem or any cellular capability.
Missing allocations may mean the AP transport has not created them. Existing
allocations/counters may be stale. Return 3 means cached allocations were not
walked. An invalid snapshot requires inspection rather than an assumed layout.
The image is prepared locally and has not been booted. A fresh human Fastboot
confirmation is needed before temporary boot; the last retained device state
is GUI011 and must not be treated as a current Fastboot observation.

## Validation and continuation

Local ASan/UBSan checks pass for QRTR/QMI (100,000 malformed inputs), SMEM
(100,000 mutations) and the live-FDT guard (100,000 mutations plus the saved
handset FDT). Four Python inventory tests include 3,000 mutations and every
truncation of a synthetic tree. A freestanding ARM64 include check passes;
the bare-metal diagnostic compiles and links. Existing GUI profile isolation
also passes. None of these results establishes electrical/radio operation.

After the diagnostic, implement and validate MPSS lifecycle plus SMEM/IPCC/
SMP2P/GLINK before QRTR service discovery. Then use the four status queries to
establish SIM presence and network state. Data needs IPA/GSI DMA, owned SMMU
contexts, multiplexing and an XNU network interface, followed by Apple PDP and
CommCenter integration. Calls additionally need VOICE/IMS signaling, carrier
configuration and the ADSP/codec microphone/earpiece route.

Keep a separate pass/fail record for each of the four carriers: SIM/slot,
registration, data/APN, IPv4/IPv6, calls in both directions with bidirectional
audio, IMS status, user roaming toggle and an observed domestic roaming
transition where the SIM/network supports one. Do not infer roaming solely
from an MCC/MNC mismatch, assume a universal APN/IMS profile, or count a QMI
success response as usable internet or a completed call.

## Protocol and driver references

Source inspection used MiCode commit
`53ef33eacdc4f7ac737451e4cfc91963d59e3956`:

- [MPSS PAS lifecycle](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/53ef33eacdc4f7ac737451e4cfc91963d59e3956/drivers/remoteproc/qcom_q6v5_pas.c)
- [SMEM](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/53ef33eacdc4f7ac737451e4cfc91963d59e3956/drivers/soc/qcom/smem.c), [GLINK SMEM](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/53ef33eacdc4f7ac737451e4cfc91963d59e3956/drivers/rpmsg/qcom_glink_smem.c)
- [QRTR](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/53ef33eacdc4f7ac737451e4cfc91963d59e3956/net/qrtr/qrtr.c), [QMI header](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/53ef33eacdc4f7ac737451e4cfc91963d59e3956/include/linux/soc/qcom/qmi.h)

Service/message numbers were cross-checked against libqmi's
[service enum](https://github.com/linux-mobile-broadband/libqmi/blob/main/src/libqmi-glib/qmi-enums.h),
[DMS](https://github.com/linux-mobile-broadband/libqmi/blob/main/data/qmi-service-dms.json),
[NAS](https://github.com/linux-mobile-broadband/libqmi/blob/main/data/qmi-service-nas.json),
[UIM](https://github.com/linux-mobile-broadband/libqmi/blob/main/data/qmi-service-uim.json) and
[WDS](https://github.com/linux-mobile-broadband/libqmi/blob/main/data/qmi-service-wds.json)
definitions on 2026-10-21. These helpers are original implementations of the
wire layouts; no Linux driver source is linked into XNU by this change.
