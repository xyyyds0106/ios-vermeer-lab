"""Verify fixed public SM8550 firmware and emit LOCAL strong Mach-O assembly.

No network or device access. Never writes in this Git checkout. Inputs remain
external local dependencies; output objects/private images must not be uploaded
by this helper. Native firmware parser and real SCM/PAS still qualify startup.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct

COMMIT = "30979b116b5c5857b72c4332db8db0ff1ca2dc08"
PINS = {
    "gmu_gen70200.bin": ("gmu", 66844, "1a2a419c39046d3141fc5fed5aa7f971de2db40cc7a1d89693c3e26fad64dd98"),
    "a740_sqe.fw": ("sqe", 76724, "96fee336424b139100fc60b5b45a907360e4b3936d7e1d00406b9bd80ca48473"),
    "a740_zap.mbn": ("zap", 12088, "386bbdc25ae94a9398e33e7580eecfdcef6c472682a1e3123e1374bcffe7cde3"),
}

def verify(root):
    records = []
    for name, (role, size, digest) in PINS.items():
        path = (root / name).resolve()
        data = path.read_bytes()
        if len(data) != size or hashlib.sha256(data).hexdigest() != digest:
            raise ValueError("Firmware identity differs: " + name)
        source = "qcom/" + ("sm8550/" if role == "zap" else "") + name
        records.append(dict(role=role, path=str(path), bytes=size, sha256=digest,
                            source="https://kernel.googlesource.com/pub/scm/linux/kernel/git/firmware/linux-firmware/+/" + COMMIT + "/" + source))
        if role == "gmu":
            at, blocks = 0, []
            while at < len(data):
                if len(data)-at < 16:
                    raise ValueError("Truncated GMU header")
                address, count, kind, value = struct.unpack_from("<4I", data, at)
                at += 16
                if count > len(data)-at or count % 4:
                    raise ValueError("Truncated/unaligned GMU payload")
                blocks.append((address, count, kind, value))
                at += count
            if len(blocks) != 15:
                raise ValueError("GMU block count differs")
            records[-1]["blocks"] = len(blocks)
        elif role == "zap":
            if data[:7] != b"\x7fELF\x01\x01\x01":
                raise ValueError("Unsupported MDT ELF")
            at = struct.unpack_from("<I", data, 28)[0]
            stride, count = struct.unpack_from("<HH", data, 42)
            if stride != 32 or not 2 <= count <= 16 or at+count*32 > len(data):
                raise ValueError("Invalid MDT program headers")
            headers = [struct.unpack_from("<8I", data, at+i*32) for i in range(count)]
            load = [h for h in headers if h[0] == 1 and (h[6] >> 24) & 7 != 2 and h[5]]
            if not load or max((h[3]+h[5]+4095) & ~4095 for h in load)-min(h[3] for h in load) > 0x2000:
                raise ValueError("MDT does not fit exact K70 reservation")
            records[-1]["load_segments"] = len(load)
            records[-1]["pas_id"] = 13
    return records

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--out", type=Path, help="External LOCAL build directory; omitted verifies only")
    args = parser.parse_args()
    records = verify(args.root)
    if args.out:
        out = args.out.resolve()
        checkout = Path(__file__).resolve().parents[2]
        if out == checkout or checkout in out.parents:
            raise ValueError("Firmware embedding output must stay outside Git checkout")
        out.mkdir(parents=True, exist_ok=True)
        assembly = [".section __DATA_CONST,__const"]
        for r in records:
            label = "_k70_" + r["role"] + "_image"
            assembly += [".p2align 14", ".globl " + label, label + ":",
                         ".incbin " + json.dumps(r["path"].replace("\\", "/")), label + "_end:",
                         ".p2align 3", ".globl " + label + "_size", label + "_size:",
                         ".quad " + label + "_end-" + label]
        (out / "k70-gpu-firmware.S").write_text("\n".join(assembly)+"\n", encoding="utf-8")
        (out / "firmware-identity.json").write_text(json.dumps(dict(source_commit=COMMIT, files=records,
            hardware_loaded=False, scm_authenticated=False, binary_publication=False), indent=2)+"\n", encoding="utf-8")
    print("PASS: fixed public GMU15-block, SQE1.81 and PAS13 MDT identities; local dependency only")

if __name__ == "__main__":
    main()
