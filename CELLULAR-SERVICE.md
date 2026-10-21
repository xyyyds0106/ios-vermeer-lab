# Isolated cellular service candidate

This is an offline-tested service/receive-adapter candidate, not an operational
K70 modem driver. It is not included by a kernel hook, profile or build. No phone
I/O, SIM setting, registration attempt, call, SMS, roaming change, firmware upload
or partition write was performed. The existing BASEBAND and observability work
is unchanged. Diagnostic012 still proves an external abort at `0x81db1000`;
diagnostic013 still has no observed execution/metadata result.

## Implemented boundary

`xnu/vermeer/cellular/vermeer_cellular.h` is a freestanding C11, allocation-free
service state machine. `vermeer_cellular_qrtr.h` adds a bounded QRTR v1/v3 receive
adapter using the existing decoder. Neither contains a native transport, physical
memory access, thread creation, device handle, socket, radio command runner or
CommCenter shim. All output buffers are values for a future native provider.

| Operation | Implemented result | Limit |
| --- | --- | --- |
| Bind/disconnect | Explicit local endpoint, verified peer node, increasing generation | Binding is not modem readiness; no MPSS start |
| Service discovery intake | Five exact service + full-instance pairs; endpoint collision/change rejected | Native namespace lookup/HELLO is absent; no first-server fallback |
| DMS `0x2d` | Operating mode byte with freshness | Unknown modes do not grant online eligibility |
| NAS `0x24` | Registration, CS/PS attach, network, explicit roaming, optional numeric MCC/MNC | No network selection/attach writer; no operator-name or MNC-width inference |
| UIM `0x2f` | QMI operation result and opaque card-status TLV presence | No card/application/slot semantic decoder, SIM-ready state or PIN operation |
| WDS `0x22` | Packet connection status with freshness | A reported connection is not an IP interface or internet test |
| VOICE `0x20` / `0x21` | Bounded number/call-ID requests, retained modem errors | Returned call ID is not call connection, IMS, incoming calling or audio |
| WDS `0x20` / `0x21` | Explicit APN/IP family, autoconnect disabled, start handle, handle-specific stop | No WDA/IPA/GSI/mux/SMMU/network interface; handle is not usable data |
| Data/roaming policy | Default data off, roaming off; unknown roaming refuses data; live gate closes on expiry/loss/change | Local policy only; no SIM/NV/carrier profile or registration change |

QMI success needs its mandatory supported response fields. A missing data handle
or call ID after a successful mutation result is **uncertain**, not success.
Unsupported indications are consumed for QRTR credit but cannot satisfy a pending
response. QMI failure retains its original error number. No automatic retry,
redial, attach or data recovery action is issued.

APNs must be explicitly supplied (1..100 ASCII characters, DNS-style labels
1..63, letters/digits/internal hyphens). IPv4 and IPv6 are separate requests;
dual-stack, credentials and profile persistence are unsupported. No universal
APN, carrier name, IMS setting or MCC/MNC-to-carrier table is installed. Numeric
PLMN is observation only: it lacks the MNC digit count and cannot identify a
subscription, domestic roaming or an operator unambiguously. `allow_roaming`
accepts an explicit NAS roaming indication; it does not classify domestic versus
international roaming. Unknown roaming stays blocked even when the toggle is on.

NAS roaming wire value **0 means roaming; 1 means home**. Other values are unknown
in this subset. Data start needs fresh DMS online, NAS registered/PS-attached,
known allowed roaming and a fresh WDS disconnected response. Responses expire
after 5000 ms. Starting a query invalidates its previous snapshot immediately.
Dial preparation needs fresh online/registered state, explicit per-request
permission, and at most 32 digits with an optional leading plus. This is a
bounded QMI control candidate; IMS/emergency/supplementary dialing is unsupported.

## Native interface candidates for coordination

Do not integrate these candidates into an already-frozen image. The owner must
first supply MPSS lifecycle and an owned, observed GLINK `IPCRTR` channel, not a
pointer to the SMEM range that faulted. The required native contracts are:

1. Initialize one `struct vcell` **once per boot**, then serialize all API calls
   on the same worker/common lock. `vcell_connect` binds a genuinely established
   point-to-point QRTR route and caller-selected full service instances. Never
   use SMEM host 1 as an inferred QRTR node or treat the five desired IDs as
   discovered services. The native QRTR namespace must perform HELLO/lookup,
   route ownership, TX credits, link/restart observation and bounded queues.
2. Route validated QRTR control/data frames through `vcell_qrtr_receive`. It
   verifies peer, destination, source port and framing before dispatch. The
   supported direct-peer control path requires source/destination control ports;
   forwarded namespace traffic needs a separately reviewed provenance adapter.
   NEW/DEL_SERVER updates exact pairs, BYE invalidates everything. Native EXIT,
   DEL_CLIENT, subsystem restart and physical link loss must also call
   `vcell_disconnect`; they are not interpreted by this receive subset.
3. For `confirm_rx`, the adapter builds the 52-byte v1 RESUME_TX packet. The
   supplied `vcell_qrtr_emit` callback must copy **all** bytes to the owned native
   control queue before returning 1, without reentering or retaining a pointer.
   Failure stops the service and publishes no response state. Acceptance into a
   queue is not proof of wire delivery; later send failure must disconnect.
   No callback exists in this repository that sends these bytes to a phone.
4. `vcell_prepare` produces a QMI request value without sending. Under the common
   lock, revalidate its generation/transaction using `vcell_pending_request`,
   atomically copy that fresh value through the native QRTR data sender, and call
   `vcell_send_complete` **before releasing that lock**. The native sender supplies
   the local source, exact discovered destination, QRTR framing and flow control.
   A retained preparation output is not a send lease. No blocking hardware I/O
   belongs under this lock; only a bounded queue-copy operation does.
5. Send completion 1 means a whole datagram was accepted; 0 means definitely no
   bytes were accepted, including EAGAIN; negative means delivery is uncertain.
   Poll `vcell_tick` independently of RX so silence expires transactions. Time
   must be monotonic; deadline equality expires. Per-context 16-bit transaction
   IDs never wrap or reset on reconnect. After exhaustion, refuse further work;
   replacing this context without draining the native client is unsafe.
6. `permit=1` is required separately for every mutating request. This flag is
   caller authorization bookkeeping, not a security boundary or proof of user
   approval. This task authorizes **no live mutation**. Gate a future IP interface
   using `vcell_user_plane_allowed` under the same serialization and on a periodic
   timer. Its true value only permits the user plane; it does not prove that IPA
   or an IP route exists. On false, close the user plane immediately. Inspect
   `vcell_stop_needed` and arrange an explicitly authorized stop if needed.

Policy changes cancel an unsent data start. If start was already sent, the gate
closes while waiting for its handle and a stop becomes required. Policy changes
with an existing handle require teardown even for an APN/IP-family change.
Preparing stop closes the gate immediately; rejected/failed stop retains the
handle and closed gate for deliberate recovery. Snapshot expiry closes the gate
without producing a hidden radio request. There is no background mutation loop.

A transmitted mutation without a valid matched result, or loss of a known data
handle/call ID, latches `uncertain` for this context, clears usable state and
blocks further mutations even after a newer generation reconnects. Read-only
queries may resume after fresh service discovery. There is deliberately no API
that clears uncertainty: an integration owner must reconcile remote resources
and native-client lifetime under separately reviewed recovery. Resetting the
context to retry would defeat the contract. A definitely unsent request can be
prepared again with a new transaction. Known QMI rejection can be reviewed and
retried explicitly; it never schedules itself. Call numbers/APNs must not be
logged; request storage is cleared on completion but is not a cryptographic
secure-erasure facility.

## Remaining path to actual cellular service

| Layer | Current evidence / missing implementation |
| --- | --- |
| MPSS/PAS/firmware/power | DT resource inventory only; live lifecycle/ownership unproven |
| SMEM/IPCC/SMP2P/GLINK | Offline metadata helpers; 012 external abort, 013 unknown; no observed readable partition or live transport |
| QRTR | Bounded frame decoder and new receive/credit candidate; native namespace, TX flow control/queues and restart source absent |
| QMI | Tested subset and session contracts; no observed service list or modem responses |
| Android RIL | No usable RIL backend here; Android binder/vendor RIL is not an iOS interface |
| Data plane | IPA/GSI DMA ownership, SMMU, WDA/multiplexing, IP/DNS/MTU, XNU interface and routing absent |
| iOS | No CommCenter/Apple PDP/IOKit adapter contract verified; GUI011 crash evidence does not establish a selector ABI |
| Calls | No IMS registration, call-status/incoming-call integration, carrier profile or bidirectional ADSP/codec audio |

The next host-verifiable milestone is a reviewed native QRTR-provider contract
with an RX/TX/restart fixture from the actual platform's intended interface,
without device access. The next hardware milestone, only after owner coordination
and fresh authorization, is an observable MPSS/GLINK route and real service
announcements, followed by the four read-only queries. Calls, network registration,
data activation and roaming remain separately authorized, observable tests.

China Mobile, China Unicom, China Telecom and China Broadnet each still have
**zero hardware passes** for SIM, registration, APN/data, IPv4/IPv6, incoming/
outgoing calls, two-way audio, IMS and domestic roaming. Synthetic fixtures do
not change that matrix.

## Validation

Run from the repository root:

```powershell
& tests/cellular/run-cellular-tests.ps1 -Analyze
```

```sh
sh tests/cellular/run-cellular-tests.sh
```

The Windows runner uses GCC C11 warnings-as-errors, optional `-fanalyzer`, both
host suites and native freestanding syntax checks. The Linux/WSL runner uses
Clang ASan/UBSan and freestanding AArch64 syntax checks. Both suites contain only
synthetic frames and do not load a native provider. Coverage includes 100000
malformed QMI inputs plus 100000 malformed QRTR frames, every response/frame
truncation, independent golden request/RESUME_TX bytes, exact instance/endpoint/
transaction checks, lost/uncertain sends, deadline/clock/restart/reconnect,
snapshot expiry before send, all four mutation failures, policy changes while
start is pending, and stop/hangup recovery. A call ID or data handle in fixtures
is never presented as a real-world success. Final commands, hashes and output
are recorded privately in `device-audit/cellular-functional-handoff.json` in
the workspace parent.

## Wire references checked 2026-10-21

Original encoders implement bounded subsets of upstream wire descriptions; no
Linux/libqmi implementation is linked. Service IDs, message/TLV fields and enum
values were checked against official libqmi definitions:
[services](https://github.com/linux-mobile-broadband/libqmi/blob/main/src/libqmi-glib/qmi-enums.h),
[VOICE](https://github.com/linux-mobile-broadband/libqmi/blob/main/data/qmi-service-voice.json),
[WDS](https://github.com/linux-mobile-broadband/libqmi/blob/main/data/qmi-service-wds.json),
[NAS](https://github.com/linux-mobile-broadband/libqmi/blob/main/data/qmi-service-nas.json),
[UIM](https://github.com/linux-mobile-broadband/libqmi/blob/main/data/qmi-service-uim.json),
[NAS enums](https://github.com/linux-mobile-broadband/libqmi/blob/main/src/libqmi-glib/qmi-enums-nas.h)
and [WDS enums](https://github.com/linux-mobile-broadband/libqmi/blob/main/src/libqmi-glib/qmi-enums-wds.h).
These links track upstream main; they are not evidence of the handset's supported
services. QRTR framing/RESUME_TX was checked against the retained MiCode
`53ef33eacdc4f7ac737451e4cfc91963d59e3956`
[qrtr.c](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/53ef33eacdc4f7ac737451e4cfc91963d59e3956/net/qrtr/qrtr.c)
and
[qrtr.h](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/53ef33eacdc4f7ac737451e4cfc91963d59e3956/include/uapi/linux/qrtr.h).
