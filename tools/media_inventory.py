"""Read a saved K70 FDT for Bluetooth/camera dependencies. No device access.

Only hardware configuration properties are emitted, never MAC addresses,
serial numbers, EEPROM contents or firmware. A node/alias is configuration,
not evidence of a powered controller, an installed sensor or a working driver.
Register tuples stay in their parent bus address space; do not use this report
as an MMIO allowlist. Output from a device dump belongs outside the public repo.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def u32s(value):
    if len(value) % 4:
        raise ValueError("unaligned cell property")
    return list(struct.unpack(">" + "I" * (len(value) // 4), value))


def strings(value):
    if not value or value[-1] != 0:
        raise ValueError("unterminated string property")
    return value[:-1].decode("ascii").split("\0")


def parse_fdt(blob):
    if not 40 <= len(blob) <= 16 * 1024 * 1024:
        raise ValueError("unsupported FDT size")
    magic, total, start, names, reserve, version, last, _, ns, size = struct.unpack_from(
        ">10I", blob)
    if magic != 0xd00dfeed or version < 17 or last > 17 or not 40 <= total <= len(blob):
        raise ValueError("invalid FDT header")
    if (start < 40 or start % 4 or size < 4 or start + size > total or
            names < 40 or names + ns > total or reserve < 40 or reserve % 8 or
            max(start, names) < min(start + size, names + ns)):
        raise ValueError("invalid FDT blocks")
    # Check the reserve map terminates without overlapping either block.
    pos = reserve
    while True:
        if pos + 16 > total or any(max(pos, a) < min(pos + 16, b)
                                  for a, b in ((start, start + size), (names, names + ns))):
            raise ValueError("invalid reserve map")
        a, n = struct.unpack_from(">QQ", blob, pos)
        pos += 16
        if not (a or n):
            break
    pos, end, stack, nodes, closed = start, start + size, [], {}, False
    while pos + 4 <= end:
        token = struct.unpack_from(">I", blob, pos)[0]
        pos += 4
        if token == 1:
            z = blob.find(b"\0", pos, end)
            if z < 0 or z - pos > 255 or len(stack) >= 128 or closed:
                raise ValueError("invalid node name/depth")
            name = blob[pos:z].decode("ascii")
            if "/" in name or (not stack and (name or nodes)) or (stack and not name):
                raise ValueError("invalid node path")
            stack.append(name)
            path = "/".join(stack) or "/"
            if path in nodes or len(nodes) >= 32768:
                raise ValueError("duplicate/too many nodes")
            nodes[path] = {}
            pos = (z + 4) & ~3
        elif token == 2:
            if not stack:
                raise ValueError("unbalanced node")
            stack.pop()
            closed = not stack
        elif token == 3:
            if not stack or pos + 8 > end:
                raise ValueError("property outside node")
            n, index = struct.unpack_from(">II", blob, pos)
            pos += 8
            if n > 1024 * 1024 or pos + n > end or index >= ns:
                raise ValueError("invalid property bounds")
            z = blob.find(b"\0", names + index, names + ns)
            if z < 0:
                raise ValueError("unterminated property name")
            key = blob[names + index:z].decode("ascii")
            props = nodes["/".join(stack) or "/"]
            if not key or key in props:
                raise ValueError("empty/duplicate property")
            props[key] = blob[pos:pos + n]
            pos = (pos + n + 3) & ~3
        elif token == 9:
            if stack or not closed or pos != end:
                raise ValueError("invalid FDT end")
            return nodes
        elif token != 4:
            raise ValueError("unknown FDT token")
    raise ValueError("missing FDT end")


TEXT = {"compatible", "status", "clock-names", "reg-names", "regulator-names",
        "pinctrl-names", "gpio-req-tbl-label", "function", "pins",
        "regulator-name", "interrupt-names", "interconnect-names", "mbox-names",
        "clock-cntl-level", "src-clock-name", "cam-smmu-label",
        "qcom,iommu-faults", "sensor-name", "qcom,sensor-name"}
CELLS = {"reg", "ranges", "#address-cells", "#size-cells", "interrupts",
         "cell-index", "cci-master", "csiphy-sd-index", "clock-rates",
         "reg-cam-base", "rgltr-min-voltage", "rgltr-max-voltage",
         "rgltr-load-current", "gpio-reset", "gpio-req-tbl-num",
         "gpio-req-tbl-flags", "drive-strength", "regulator-min-microvolt",
         "regulator-max-microvolt", "qcom,wakeup-byte", "qcom,converged-dt",
         "bias-pull-down", "bias-pull-up", "bias-disable", "output-low",
         "output-high", "input-enable", "dma-coherent", "no-map",
         "qcom,iommu-dma-addr-pool", "qcom,slave-id", "multiple-client-devices"}
REF = {"clocks": "#clock-cells", "iommus": "#iommu-cells",
       "power-domains": "#power-domain-cells", "mboxes": "#mbox-cells",
       "interconnects": "#interconnect-cells", "gpios": "#gpio-cells",
       "interrupts-extended": "#interrupt-cells", "interrupt-parent": None,
       "memory-region": None, "eeprom-src": None, "actuator-src": None,
       "led-flash-src": None}


def parent(path):
    return path.rsplit("/", 1)[0] or "/"


def inventory(blob):
    nodes = parse_fdt(blob)
    handles = {}
    for path, props in nodes.items():
        values = [u32s(props[k]) for k in ("phandle", "linux,phandle") if k in props]
        if not values:
            continue
        if any(len(v) != 1 or v[0] in (0, 0xffffffff) or v != values[0] for v in values):
            raise ValueError("invalid/inconsistent phandle")
        h = values[0][0]
        if h in handles:
            raise ValueError("duplicate phandle")
        handles[h] = path

    def compatible(props):
        return strings(props["compatible"]) if "compatible" in props else []

    def availability(path):
        chain = []
        while True:
            status = strings(nodes[path]["status"])[0] if "status" in nodes[path] else "okay"
            if status not in ("ok", "okay"):
                chain.append({"path": path, "status": status})
            if path == "/":
                break
            path = parent(path)
        return {"configured_available": not chain, "disabled_by": chain}

    bt = [p for p, d in nodes.items() if any(
        c in ("qcom,bt-qca-converged", "qcom,kiwi", "qcom,qca6490") for c in compatible(d))]
    camera = [p for p, d in nodes.items() if any(
        c.startswith(("qcom,cam-", "qcom,cam_", "qcom,csiphy", "qcom,cci",
                      "qcom,ife", "qcom,ipe", "qcom,icp", "qcom,bps", "qcom,jpeg",
                      "qcom,vfe", "qcom,sfe", "qcom,csid", "qcom,msm-cam-smmu",
                      "qcom,camera-flash"))
        for c in compatible(d))]
    aliases = {}
    for key in ("hsuart0", "serial0"):
        value = nodes.get("/aliases", {}).get(key)
        if value:
            target = strings(value)
            if len(target) != 1 or target[0] not in nodes:
                raise ValueError("unresolved UART alias")
            aliases[key] = {"path": target[0], **availability(target[0])}
    pending = list(dict.fromkeys(bt + camera + [v["path"] for v in aliases.values()]))
    selected = {}
    while pending:
        path = pending.pop(0)
        if path in selected:
            continue
        props = nodes[path]
        row = {**availability(path), "properties": {}, "references": {}}
        selected[path] = row
        for key in sorted((TEXT | CELLS) & props.keys()):
            row["properties"][key] = strings(props[key]) if key in TEXT else u32s(props[key])
        if "reg" in props and path != "/":
            bus = nodes[parent(path)]
            ac = u32s(bus.get("#address-cells", struct.pack(">I", 2)))
            sc = u32s(bus.get("#size-cells", struct.pack(">I", 1)))
            if len(ac) != 1 or len(sc) != 1 or ac[0] not in (1, 2) or sc[0] not in (0, 1, 2):
                raise ValueError("unsupported bus cell layout")
            values, width = u32s(props["reg"]), ac[0] + sc[0]
            if len(values) % width:
                raise ValueError("truncated reg tuple")
            row["registers_parent_bus"] = [{
                "address": hex(int.from_bytes(props["reg"][4*i:4*(i+ac[0])], "big")),
                "size": hex(int.from_bytes(props["reg"][4*(i+ac[0]):4*(i+width)], "big"))
            } for i in range(0, len(values), width)]
            row["bus"] = parent(path)
        for key, value in props.items():
            pin = key.startswith("pinctrl-") and key[8:].isdigit()
            gpio = key.endswith("-gpio") or key.endswith("-gpios")
            ref = key in REF or key.endswith("-supply") or pin or gpio
            if key.startswith("qcom,bt-vdd") and key.endswith("-config"):
                row["properties"][key] = u32s(value)
            if not ref:
                continue
            count_key = "#gpio-cells" if gpio else REF.get(key)
            values, refs, pos = u32s(value), [], 0
            while pos < len(values):
                h = values[pos]
                pos += 1
                target = handles.get(h)
                if target is None:
                    raise ValueError(f"unresolved reference {path}:{key}")
                count = u32s(nodes[target].get(count_key, b"")) if count_key else [0]
                if len(count) != 1 or count[0] > 16 or pos + count[0] > len(values):
                    raise ValueError(f"invalid reference arguments {path}:{key}")
                refs.append({"path": target, "args": values[pos:pos + count[0]]})
                pos += count[0]
                pending.append(target)
                if pin:
                    pending.extend(p for p in nodes if p.startswith(target + "/"))
            row["references"][key] = refs
        if path != "/":
            pending.append(parent(path))
    candidates = [p for p in bt if "qcom,bt-qca-converged" not in compatible(nodes[p])]
    sensors = [p for p in camera if "qcom,cam-sensor" in compatible(nodes[p])]
    return {"input_sha256": hashlib.sha256(blob).hexdigest(),
            "evidence": "saved FDT configuration only; no live hardware access",
            "bluetooth": {"candidate_nodes": candidates,
                          "chip_identity_verified": False, "uart_aliases": aliases},
            "camera": {"sensor_slot_nodes": sensors, "sensor_identity_verified": False},
            "nodes": dict(sorted(selected.items()))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dtb", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = inventory(args.dtb.read_bytes())
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Saved {len(report['nodes'])} dependency nodes, "
          f"{len(report['bluetooth']['candidate_nodes'])} Bluetooth candidates, "
          f"{len(report['camera']['sensor_slot_nodes'])} camera slots; identities unverified")


if __name__ == "__main__":
    main()
