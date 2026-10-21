# K70 USB debug console

Experimental opt-in CDC ACM serial interface, integrated with the iOS profile.
Hardware checkpoint2026-10-21: integrated004 (source `f0aec9c`,
Actions39540409885) enumerated with Windows Usbser as COM6, streamed early
kernel and Apple-service crash logs, and answered `status` through bulk OUT/IN.
No USB errors appeared during that capture. This does not establish iOS GUI,
panic-time transport or reconnect support after a physical disconnect.
The initial capture saved108,604bytes; a subsequent close/reopen test passed
`ping`, `help`, `status` and retained early-log `replay` on the same boot.

Boot with `vermeer_usb=1`. The default and diagnostic profiles do not start USB.
Windows uses its built-in Usbser driver for CDC ACM (class02/subclass02).
The laboratory VID/PID is1209:0001; synthetic serial `K70DBG004`.

The interface streams the most recent1MiB of console text, then live output.
Open the COM port with DTR asserted. Speed115200/8N1 is informational on USB.
`tools/capture-debug.ps1` finds this exact synthetic device and records bytes,
rotating at32MiB per file. Captures and device information stay outside Git.

Commands, each followed by a newline:

- `help`: command list
- `ping`: build-tag response
- `status`: event/byte counters and dropped console bytes
- `replay`: replay the retained RAM log
- `sample`: queue a read-only snapshot of the GUI sampler's boot-selected
  processes, or report unavailable if disabled/the40-snapshot limit is reached
- `frame` (GUI009): submitted/presented frame counts, vsync activity, copied
  bytes and missing/rejected surfaces; reports the current CPU copy backend
- `input` (GUI009): touch entry and HID dispatch counts; these are observations,
  not a touch injection command or proof of a physical controller driver

`-ObserveFrames` makes the capture tool query `frame`/`input` every ten seconds
while continuing to collect logs on the same serial connection. Both callbacks
read counters only; they do not take a display/HID lock or access hardware.
The new counters still require GUI009 real-device validation.

The GUI007 sampler records saved user thread stacks and bounded dyld64 private
image paths/bases, allowing migration-plugin addresses to be symbolicated.
Work runs on a separate sampler thread. Requests coalesce and snapshots are at
least10seconds apart; the timer starts at90seconds, then every120seconds.
`sweetsample=-` disables it. Names use the kernel's16-character process name.
The host cannot supply addresses, task names or read sizes. Image enumeration
uses fault-safe reads, at most4096entries/64private rows and159path bytes per
row; a changing image list is marked best effort. The shared cache uses its
separately recorded slide. Stack lines are split into four-frame chunks so the
console formatter retains line boundaries. GUI007 and GUI008 pass manual USB
sampling and post-sample ping on the test handset, including GUI008 after the
user reports a frozen picture.

No shell, arbitrary memory access, storage operations or reboot commands are exposed.
This is log transport and bounded queries, not an interactive kernel debugger.
After a cable disconnect, configured bus reset, overflow or DMA/endpoint error,
the interface stops and retains all DMA memory. Reboot the temporary kernel to
reconnect. iOS continues independently; panic-time polling is not implemented.

Hardware guards require the SM8550 USB power/clocks already enabled by ABL,
DWC3/3.1 device mode, sufficient DMA address width, one identity SMMU route for
SID0x40, and contiguous memory in the independently tested2GiB high-RAM range.
The driver never changes clock, PHY/repeater, PMIC or SMMU configuration; only
DWC device registers and the Qualcomm wrapper's VBUS-valid override are set.
An unsupported hardware state skips USB and leaves the normal boot running.

Hardware enumeration and live capture must be verified separately from host
tests and compilation. A screen displaying this build's USB state may still be
needed for the first attempt.

Register/protocol references:
[Linux DWC3](https://github.com/torvalds/linux/tree/v6.1/drivers/usb/dwc3),
[SM8550 GCC](https://github.com/torvalds/linux/blob/master/drivers/clk/qcom/gcc-sm8550.c),
[Windows CDC ACM](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-driver-installation-based-on-compatible-ids).
