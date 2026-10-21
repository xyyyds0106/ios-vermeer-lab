"""Decode bounded VMT1 records from the existing CDC console capture offline."""
import argparse
import json
from pathlib import Path
import re

FIELDS = (
    "strap_cfg_a", "strap_io_a", "strap_cfg_b", "strap_io_b",
    "bt_enable_cfg", "bt_enable_io", "bt_sw_cfg", "bt_sw_io",
    "camera0_reset_cfg", "camera0_reset_io", "camera1_reset_cfg", "camera1_reset_io",
    "camera2_reset_cfg", "camera2_reset_io", "camera3_reset_cfg", "camera3_reset_io",
)
STATUS = ("ok", "gated", "bounds", "read_failed", "revoked", "clock_failed", "bad_argument")
HEX8, HEX16 = r"[0-9a-f]{8}", r"[0-9a-f]{16}"
PATTERN = re.compile(
    rf"VMT1 seq=({HEX16}) start=({HEX16}|-{{16}}) end=({HEX16}|-{{16}}) "
    rf"status=({HEX8}) proof=({HEX8}) req=({HEX8}) valid=({HEX8}) fail=({HEX8}) "
    rf"uart=skipped data=((?:{HEX8}|-{{8}})(?:,(?:{HEX8}|-{{8}})){{15}})"
)


def parse_record(line):
    if len(line) > 511:
        raise ValueError("oversized VMT1 record")
    match = PATTERN.fullmatch(line)
    if not match:
        raise ValueError("incomplete or malformed VMT1 record")
    seq, start, end, status, proof, requested, valid, failed, data = match.groups()
    seq, status, proof, requested, valid, failed = (
        int(v, 16) for v in (seq, status, proof, requested, valid, failed))
    if not seq or status >= len(STATUS) or requested & ~0xffff or valid & ~requested:
        raise ValueError("invalid VMT1 metadata")
    start = None if start.startswith("-") else int(start, 16)
    end = None if end.startswith("-") else int(end, 16)
    values = {}
    for i, value in enumerate(data.split(",")):
        missing = value.startswith("-")
        if missing == bool(valid & (1 << i)):
            raise ValueError("VMT1 validity and data disagree")
        values[FIELDS[i]] = None if missing else int(value, 16)
    if failed != 0xffffffff and (failed >= 16 or not requested & (1 << failed)
                                 or valid & (1 << failed)):
        raise ValueError("invalid VMT1 failure index")
    if status == 0 and (not requested or valid != requested or proof & 31 != 31 or
                        failed != 0xffffffff or start is None or end is None or end < start):
        raise ValueError("incomplete successful VMT1 observation")
    if status in (1, 2, 6) and (valid or failed != 0xffffffff):
        raise ValueError("VMT1 preflight failure contains samples")
    if status in (3, 4) and failed == 0xffffffff:
        raise ValueError("VMT1 device failure lacks an index")
    if start is not None and end is not None and end < start and status != 5:
        raise ValueError("VMT1 backwards time lacks clock failure")
    return {"version": 1, "sequence": seq, "start_us": start, "end_us": end,
            "status": STATUS[status], "proof": proof, "requested": requested,
            "valid": valid, "failed_index": None if failed == 0xffffffff else failed,
            "uart": "skipped", "values": values}


def parse_trace(text):
    records, replayed = {}, 0
    for line in text.splitlines():
        if not line.startswith("VMT1 "):
            continue
        record = parse_record(line)
        seq = record["sequence"]
        if seq in records:
            if records[seq] != record:
                raise ValueError("conflicting VMT1 replay or mixed boot sessions")
            replayed += 1
            continue
        if records and seq <= max(records):
            raise ValueError("VMT1 sequence moved backwards; separate boot sessions")
        if len(records) >= 1024:
            raise ValueError("too many VMT1 records")
        records[seq] = record
    return {"records": list(records.values()), "identical_replays": replayed,
            "hardware_identity_verified": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.capture.stat().st_size > 16 * 1024 * 1024:
        parser.error("capture exceeds 16 MiB; split by boot session")
    result = parse_trace(args.capture.read_text(encoding="utf-8-sig"))
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Decoded {len(result['records'])} observations, {result['identical_replays']} replays")


if __name__ == "__main__":
    main()
