"""Decode a saved diagnostic record log. No serial/ADB/Fastboot access.

This describes a future producer; current diagnostic013 emits no such records.
"""
import argparse
import json
from pathlib import Path
import struct
import zlib

PREFIX = "VBBR1 "
STATES = {1: "begin", 2: "before_read", 3: "read_ok", 4: "finished",
          5: "abort", 6: "refused"}


def decode(raw):
    if len(raw) != 128 or raw[:8] != b"VBBR\x01\x00\x80\x00":
        raise ValueError("record size/magic/version")
    if any(raw[112:124]) or zlib.crc32(raw[:124]) != struct.unpack_from("<I", raw, 124)[0]:
        raise ValueError("record reserved bytes/CRC")
    seq = struct.unpack_from("<Q", raw, 8)[0]
    state, phase, addr, size, result, esr, far, vector, flags = struct.unpack_from("<IIQIIQQII", raw, 64)
    if (not seq or state not in STATES or phase > 5 or size > 4096 or result > 5 or
            flags & ~3 or not any(raw[16:32]) or not any(raw[32:64])):
        raise ValueError("record fields")
    if state == 1:
        valid = seq == 1 and not any((phase, addr, size, result, esr, far, vector, flags))
    elif seq == 1:
        valid = False
    elif state in (4, 6):
        valid = not any((addr, size, esr, far, vector, flags))
        valid &= bool(phase and (result or phase == 5)) if state == 4 else bool(result)
    else:
        valid = (phase > 0 and result == 0 and size > 0 and not (addr % 8 or size % 8)
                 and 0x81d00000 <= addr < 0x81f00000 and size <= 0x81f00000 - addr)
        valid &= (bool(flags & 1) and vector < 16 and (bool(flags & 2) or far == 0)) if state == 5 else not any((esr, far, vector, flags))
    if not valid:
        raise ValueError("inconsistent state fields")
    return dict(sequence=seq, state=state, phase=phase, address=addr, length=size,
                result=result, esr=esr, far=far, vector=vector, flags=flags,
                run=raw[16:32].hex(), image_sha256=raw[32:64].hex())


def trace(lines, run, image_sha256):
    """Reject gaps/replays/mixed runs. Incomplete transport is never success."""
    last = None
    entries_length = None
    partition_read = None
    records = 0
    for line in lines:
        if len(line) > 4096:
            raise ValueError("oversize log line")
        if not line.startswith(PREFIX):
            continue
        encoded = line[len(PREFIX):].rstrip("\r\n")
        if len(encoded) != 256:
            raise ValueError("frame truncation/length")
        r = decode(bytes.fromhex(encoded))
        if r["run"] != run or r["image_sha256"] != image_sha256:
            raise ValueError("unexpected run/image identity")
        state, phase = r["state"], r["phase"]
        if last is None:
            if state != 1:
                raise ValueError("missing begin")
        else:
            if r["sequence"] != last["sequence"] + 1 or last["state"] in (4, 5, 6):
                raise ValueError("sequence gap/replay/record after terminal state")
            prev = last["state"]
            if state == 2:
                if prev not in (1, 3) or phase != last["phase"] + 1:
                    raise ValueError("read phase order")
                addr, size = r["address"], r["length"]
                if addr < 0x81db2000 and 0x81db1000 < addr + size:
                    raise ValueError("observed fault page requested")
                if phase == 1 and (addr, size) != (0x81eff000, 32):
                    raise ValueError("TOC header range")
                if phase == 2:
                    if addr != 0x81eff020 or size % 48 or size > 84 * 48:
                        raise ValueError("TOC entry range")
                    entries_length = size
                if phase == 3 and (addr, size) != (0x81eff000, 32 + entries_length):
                    raise ValueError("TOC recheck range")
                if phase == 4:
                    if size != 32 or addr < 0x81d00020 or addr + size > 0x81eff000:
                        raise ValueError("partition header range")
                    partition_read = (addr, size)
                if phase == 5 and (addr, size) != partition_read:
                    raise ValueError("partition recheck range")
            elif state in (3, 5):
                if prev != 2 or any(r[k] != last[k] for k in ("phase", "address", "length")):
                    raise ValueError("unmatched read result")
            elif state == 4:
                if prev != 3 or phase != last["phase"] or (r["result"] == 0 and phase != 5):
                    raise ValueError("premature completion")
            elif state == 6:
                if prev not in (1, 3) or phase not in (last["phase"], last["phase"] + 1):
                    raise ValueError("refusal order")
            else:
                raise ValueError("duplicate begin")
        last = r
        records += 1
    outcome = "unknown_no_records" if last is None else "incomplete"
    if last and last["state"] in (4, 5, 6):
        outcome = STATES[last["state"]]
    return dict(outcome=outcome, records=records, last=last,
                metadata_pass=bool(last and last["state"] == 4 and last["result"] == 0),
                modem_operation_proven=False)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("log", type=Path)
    p.add_argument("--run", required=True, help="32 hex digits from this launch request")
    p.add_argument("--image-sha256", required=True, help="64 hex digits from verified launch manifest")
    args = p.parse_args()
    try:
        if len(args.run) != 32 or len(args.image_sha256) != 64:
            raise ValueError("identity length")
        run = bytes.fromhex(args.run).hex()
        sha = bytes.fromhex(args.image_sha256).hex()
        if not any(bytes.fromhex(run)) or not any(bytes.fromhex(sha)):
            raise ValueError("zero identity")
        if args.log.stat().st_size > 32 * 1024 * 1024:
            raise ValueError("log exceeds 32 MiB")
        with args.log.open(encoding="ascii", errors="strict") as source:
            result = trace(source, run, sha)
    except (OSError, UnicodeError, ValueError) as error:
        p.exit(2, f"Invalid record log: {error}\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
