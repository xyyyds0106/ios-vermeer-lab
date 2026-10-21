"""Synthetic FDT tests: dependency decoding must not guess live identities."""
import json
from pathlib import Path
import random
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from media_inventory import inventory, parse_fdt


def cell(*values):
    return struct.pack(">" + "I" * len(values), *values)


def text(*values):
    return b"\0".join(v.encode("ascii") for v in values) + b"\0"


def fdt(tree):
    names, body = bytearray(), bytearray()
    def pad(value):
        return value + b"\0" * (-len(value) % 4)
    def node(name, props, children):
        body.extend(cell(1) + pad(name.encode() + b"\0"))
        for key, value in props.items():
            index = len(names)
            names.extend(key.encode() + b"\0")
            body.extend(cell(3, len(value), index) + pad(value))
        for child in children:
            node(*child)
        body.extend(cell(2))
    node(*tree)
    body.extend(cell(9))
    start = 56
    header = cell(0xd00dfeed, start + len(body) + len(names), start,
                  start + len(body), 40, 17, 16, 0, len(names), len(body))
    return header + bytes(16) + body + names


def fixture(disabled=False):
    return ("", {"#address-cells": cell(2), "#size-cells": cell(2)}, [
        ("aliases", {"hsuart0": text("/soc/uart@898000"),
                     "serial0": text("/soc/debug@a9c000")}, []),
        ("soc", {"#address-cells": cell(1), "#size-cells": cell(1),
                 "ranges": cell(0, 0, 0, 0xffffffff),
                 "status": text("disabled" if disabled else "okay")}, [
            ("gpio", {"phandle": cell(1), "#gpio-cells": cell(2)}, []),
            ("clock", {"phandle": cell(2), "#clock-cells": cell(1)}, []),
            ("regulator", {"phandle": cell(3), "regulator-min-microvolt": cell(1800000)}, []),
            ("bt", {"compatible": text("qcom,bt-qca-converged")}, [
                ("kiwi", {"compatible": text("qcom,kiwi"),
                          "qcom,bt-reset-gpio": cell(1, 81, 0),
                          "qcom,bt-vdd18-aon-supply": cell(3),
                          "qcom,bt-vdd18-aon-config": cell(1800000, 1800000, 0, 1),
                          "local-bd-address": b"PRIVATE"}, []),
                ("qca", {"compatible": text("qcom,qca6490")}, [])]),
            ("uart@898000", {"compatible": text("qcom,msm-geni-serial-hs"),
                             "reg": cell(0x898000, 0x4000), "clocks": cell(2, 123)}, []),
            ("debug@a9c000", {"compatible": text("qcom,geni-debug-uart"),
                              "reg": cell(0xa9c000, 0x4000)}, []),
            ("cci@ac15000", {"compatible": text("qcom,cci", "simple-bus")}, [
                ("sensor0", {"compatible": text("qcom,cam-sensor"),
                             "cell-index": cell(0), "cci-master": cell(0),
                             "csiphy-sd-index": cell(5),
                             "cam_vio-supply": cell(3), "serial-number": text("PRIVATE")}, [])])])])


class InventoryTests(unittest.TestCase):
    def test_contract_and_privacy(self):
        report = inventory(fdt(fixture()))
        self.assertEqual(len(report["bluetooth"]["candidate_nodes"]), 2)
        self.assertFalse(report["bluetooth"]["chip_identity_verified"])
        self.assertFalse(report["camera"]["sensor_identity_verified"])
        sensor = report["nodes"]["/soc/cci@ac15000/sensor0"]
        self.assertEqual(sensor["properties"]["cell-index"], [0])
        self.assertEqual(sensor["references"]["cam_vio-supply"][0]["path"], "/soc/regulator")
        uart = report["nodes"]["/soc/uart@898000"]
        self.assertEqual(uart["registers_parent_bus"], [{"address": "0x898000", "size": "0x4000"}])
        self.assertEqual(uart["references"]["clocks"][0]["args"], [123])
        self.assertNotIn("PRIVATE", json.dumps(report))
        self.assertNotEqual(report["bluetooth"]["uart_aliases"]["hsuart0"]["path"],
                            report["bluetooth"]["uart_aliases"]["serial0"]["path"])

    def test_disabled_parent(self):
        report = inventory(fdt(fixture(disabled=True)))
        row = report["nodes"]["/soc/bt/kiwi"]
        self.assertFalse(row["configured_available"])
        self.assertEqual(row["disabled_by"], [{"path": "/soc", "status": "disabled"}])

    def test_missing_reference(self):
        blob = fdt(fixture()).replace(cell(2, 123), cell(99, 123))
        with self.assertRaisesRegex(ValueError, "unresolved reference"):
            inventory(blob)

    def test_truncated_reference(self):
        # Require two specifier cells while the client supplies one.
        tree = fixture()
        tree[2][1][2][1][1]["#clock-cells"] = cell(2)
        with self.assertRaisesRegex(ValueError, "invalid reference arguments"):
            inventory(fdt(tree))

    def test_duplicate_phandle(self):
        tree = fixture()
        tree[2][1][2][1][1]["phandle"] = cell(1)
        with self.assertRaisesRegex(ValueError, "duplicate phandle"):
            inventory(fdt(tree))

    def test_reg_tuple_not_truncated(self):
        tree = fixture()
        tree[2][1][2][4][1]["reg"] = cell(0x898000)
        with self.assertRaisesRegex(ValueError, "truncated reg tuple"):
            inventory(fdt(tree))

    def test_camera_smmu_and_isp(self):
        tree = fixture()
        tree[2][1][2].extend([
            ("cam_smmu", {"compatible": text("qcom,msm-cam-smmu")}, [
                ("ife", {"compatible": text("qcom,msm-cam-smmu-cb"),
                         "cam-smmu-label": text("ife", "sfe")}, [])]),
            ("ife", {"compatible": text("qcom,vfe780")}, []),
            ("sfe", {"compatible": text("qcom,sfe780")}, []),
            ("csid", {"compatible": text("qcom,csid780")}, [])])
        report = inventory(fdt(tree))
        for path in ("/soc/cam_smmu/ife", "/soc/ife", "/soc/sfe", "/soc/csid"):
            self.assertIn(path, report["nodes"])

    def test_all_truncations(self):
        blob = fdt(fixture())
        for n in range(len(blob)):
            with self.assertRaises(ValueError):
                parse_fdt(blob[:n])

    def test_corrupt_header_and_reserve(self):
        blob = fdt(fixture())
        for offset, value in ((0, 0), (8, 41), (12, len(blob)), (16, 56), (36, 0xffffffff)):
            bad = bytearray(blob)
            struct.pack_into(">I", bad, offset, value)
            with self.assertRaises(ValueError):
                parse_fdt(bad)
        rng = random.Random(7150)
        for _ in range(2000):
            bad = bytearray(blob)
            offset = rng.randrange(len(bad))
            bad[offset] ^= rng.randrange(1, 256)
            try:
                inventory(bad)
            except ValueError:
                pass


if __name__ == "__main__":
    unittest.main()
