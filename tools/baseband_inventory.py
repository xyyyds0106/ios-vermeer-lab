"""Inspect a saved FDT for the K70 modem bring-up; never access a device.

Output contains hardware properties only, with phandles resolved. Addresses
are in the parent bus address space, not assumed CPU physical addresses.
Neither a DT status of 'okay' nor a node's presence proves live hardware.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def parse_fdt(blob):
    if not 40 <= len(blob) <= 16 * 1024 * 1024:
        raise ValueError("FDT size outside supported bounds")
    magic, total, off, strings, _, version, last, _, slen, size = struct.unpack_from(
        ">10I", blob)
    if magic != 0xD00DFEED or not 40 <= total <= len(blob) or version < 17 or last > 17:
        raise ValueError("unsupported FDT header")
    if (off < 40 or off % 4 or size < 4 or off + size > total or
            strings < 40 or strings + slen > total or
            max(off, strings) < min(off + size, strings + slen)):
        raise ValueError("invalid FDT block bounds")
    end, string_end = off + size, strings + slen
    stack, nodes = [], {}
    root_closed = False
    while off + 4 <= end:
        token = struct.unpack_from(">I", blob, off)[0]
        off += 4
        if token == 1:
            terminator = blob.find(b"\0", off, end)
            if terminator < 0 or terminator - off > 255 or len(stack) >= 128:
                raise ValueError("invalid FDT node name/depth")
            name = blob[off:terminator].decode("ascii")
            if "/" in name or root_closed or (not stack and (nodes or name)):
                raise ValueError("invalid FDT root or node name")
            stack.append(name)
            path = "/".join(stack) or "/"
            if path in nodes or len(nodes) >= 32768:
                raise ValueError("duplicate/too many FDT nodes")
            nodes[path] = {}
            off = (terminator + 4) & ~3
        elif token == 2:
            if not stack:
                raise ValueError("unbalanced FDT node")
            stack.pop()
            root_closed = not stack
        elif token == 3:
            if not stack or off + 8 > end:
                raise ValueError("property outside node or truncated header")
            length, index = struct.unpack_from(">II", blob, off)
            off += 8
            if length > 1024 * 1024 or off + length > end or index >= slen:
                raise ValueError("invalid FDT property bounds")
            terminator = blob.find(b"\0", strings + index, string_end)
            if terminator < 0:
                raise ValueError("unterminated FDT property name")
            name = blob[strings + index:terminator].decode("ascii")
            props = nodes["/".join(stack) or "/"]
            if not name or name in props:
                raise ValueError("empty/duplicate FDT property")
            props[name] = blob[off:off + length]
            off = (off + length + 3) & ~3
        elif token == 9:
            if stack or not root_closed:
                raise ValueError("unfinished FDT tree")
            return nodes
        elif token != 4:
            raise ValueError("unknown FDT token")
    raise ValueError("missing FDT end token")


def cells(value):
    if len(value) % 4:
        raise ValueError("non-cell-aligned FDT property")
    return list(struct.unpack(">" + "I" * (len(value) // 4), value))


STRINGS = {"compatible", "status", "reg-names", "clock-names", "interrupt-names",
           "firmware-name", "qcom,firmware-name", "qcom,glink-label",
           "qcom,glink-channels", "qcom,edge", "qcom,channel", "qcom,subsys-name",
           "qcom,smem-state-names", "qcom,entry-name", "mbox-names",
           "power-domain-names", "memory-region-names"}
PROPERTIES = STRINGS | {
    "reg", "#address-cells", "#size-cells", "ranges", "phandle", "linux,phandle",
    "memory-region", "no-map", "interrupts", "interrupts-extended", "interrupt-parent",
    "qcom,remote-pid", "qcom,local-pid", "qcom,smem", "qcom,smem-states", "mboxes",
    "qcom,smem-host-id", "qcom,smem-id", "clocks", "power-domains", "iommus",
    "#clock-cells", "#power-domain-cells", "#iommu-cells", "#mbox-cells",
    "#interrupt-cells", "#qcom,smem-state-cells", "qcom,ipa-hw-ver",
    "qcom,msm-bus,vectors-KBps", "qcom,msm-bus,num-cases", "qcom,msm-bus,num-paths"}
REFERENCES = {"memory-region": None, "interrupt-parent": None,
              "clocks": "#clock-cells", "power-domains": "#power-domain-cells",
              "iommus": "#iommu-cells", "mboxes": "#mbox-cells",
              "interrupts-extended": "#interrupt-cells",
              "qcom,smem-states": "#qcom,smem-state-cells"}


def inventory(blob):
    nodes = parse_fdt(blob)
    phandles = {}
    for path, props in nodes.items():
        handles = [cells(props[k]) for k in ("phandle", "linux,phandle") if k in props]
        if not handles:
            continue
        if any(len(h) != 1 or h[0] in (0, 0xffffffff) for h in handles) or any(
                h != handles[0] for h in handles):
            raise ValueError("invalid/inconsistent phandle")
        handle = handles[0][0]
        if handle in phandles:
            raise ValueError("duplicate phandle")
        phandles[handle] = path
    roots = []
    for path, props in nodes.items():
        comp = props.get("compatible", b"").rstrip(b"\0").split(b"\0")
        if (any(c in comp for c in (b"qcom,kalama-modem-pas", b"qcom,smem",
                                   b"qcom,ipcc", b"qcom,ipa")) or
                path.endswith("/qcom,smp2p-modem")):
            roots.append(path)
    pending = [p for p in nodes if any(p == r or p.startswith(r + "/") for r in roots)]
    selected = {}
    while pending:
        path = pending.pop(0)
        if path in selected:
            continue
        props = nodes[path]
        parent = path.rsplit("/", 1)[0] or "/"
        row = {"properties": {}, "references": {}}
        selected[path] = row
        for key in sorted(PROPERTIES & props.keys()):
            value = props[key]
            row["properties"][key] = (value.rstrip(b"\0").decode("ascii").split("\0")
                                        if key in STRINGS else value.hex())
        if "reg" in props:
            ac = cells(nodes[parent].get("#address-cells", struct.pack(">I", 2)))
            sc = cells(nodes[parent].get("#size-cells", struct.pack(">I", 1)))
            if len(ac) != 1 or len(sc) != 1 or ac[0] not in (1, 2) or sc[0] not in (0, 1, 2):
                raise ValueError("unsupported reg cell layout")
            values, width = cells(props["reg"]), ac[0] + sc[0]
            if len(values) % width:
                raise ValueError("truncated reg tuple")
            row["registers_parent_bus"] = []
            for i in range(0, len(values), width):
                address = int.from_bytes(props["reg"][4*i:4*(i+ac[0])], "big")
                length = int.from_bytes(props["reg"][4*(i+ac[0]):4*(i+width)], "big")
                row["registers_parent_bus"].append({"address": hex(address), "size": hex(length)})
            row["parent"] = parent
            # Retain bus translation information without silently applying it.
            row["parent_ranges_hex"] = nodes[parent].get("ranges", b"").hex()
            row["parent_ranges_present"] = "ranges" in nodes[parent]
        for key, count_key in REFERENCES.items():
            if key not in props:
                continue
            values, pos, refs = cells(props[key]), 0, []
            while pos < len(values):
                handle = values[pos]
                pos += 1
                target = phandles.get(handle)
                if target is None:
                    raise ValueError(f"unresolved {key} phandle {handle:#x}")
                count = 0
                if count_key:
                    spec = cells(nodes[target].get(count_key, b""))
                    if len(spec) != 1 or spec[0] > 16:
                        raise ValueError(f"invalid {count_key} on {target}")
                    count = spec[0]
                if count > len(values) - pos:
                    raise ValueError(f"truncated {key} arguments")
                refs.append({"path": target, "args": values[pos:pos+count]})
                pos += count
                pending.append(target)
            row["references"][key] = refs
    return {"source_sha256": hashlib.sha256(blob).hexdigest(),
            "evidence": "saved-device-tree-only; no live modem or firmware validation",
            "roots": sorted(roots), "nodes": dict(sorted(selected.items()))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dtb", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result = inventory(args.dtb.read_bytes())
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Saved hardware inventory: {len(result['roots'])} roots, {len(result['nodes'])} nodes")


if __name__ == "__main__":
    main()
