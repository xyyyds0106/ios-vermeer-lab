"""Synthetic hardware trees only: no handset dump needed in CI."""
from pathlib import Path
import random
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from baseband_inventory import inventory, parse_fdt


def u32(*values):
    return struct.pack(">" + "I" * len(values), *values)


def fixture(extra=(), ref=1):
    strings, body = bytearray(), bytearray()

    def node(name):
        body.extend(u32(1) + name.encode() + b"\0")
        body.extend(b"\0" * (-len(body) % 4))

    def prop(name, value):
        offset = len(strings)
        strings.extend(name.encode() + b"\0")
        body.extend(u32(3, len(value), offset) + value)
        body.extend(b"\0" * (-len(body) % 4))

    node("")
    prop("#address-cells", u32(2)); prop("#size-cells", u32(2))
    prop("serial-number", b"DO-NOT-EXPORT\0")
    node("reserved-memory")
    prop("#address-cells", u32(2)); prop("#size-cells", u32(2)); prop("ranges", b"")
    node("mpss@old-name")
    prop("phandle", u32(1)); prop("reg", u32(0, 0x8a800000, 0, 0x10800000))
    body.extend(u32(2, 2))
    node("soc")
    prop("#address-cells", u32(1)); prop("#size-cells", u32(1))
    prop("ranges", u32(0, 0, 0, 0xffffffff))
    node("remoteproc-mss@04080000")
    prop("compatible", b"qcom,kalama-modem-pas\0")
    prop("reg", u32(0x4080000, 0x10000)); prop("memory-region", u32(ref))
    for name, value in extra:
        prop(name, value)
    body.extend(u32(2, 2, 2, 9))
    off = 56  # header plus empty reserve map
    return u32(0xd00dfeed, off+len(body)+len(strings), off, off+len(body),
               40, 17, 16, 0, len(strings), len(body)) + bytes(16) + body + strings


class InventoryTest(unittest.TestCase):
    def test_real_reg_and_phandle_not_node_unit_name(self):
        report = inventory(fixture())
        modem = report["nodes"]["/soc/remoteproc-mss@04080000"]
        self.assertEqual(modem["references"]["memory-region"][0]["path"],
                         "/reserved-memory/mpss@old-name")
        ram = report["nodes"]["/reserved-memory/mpss@old-name"]
        self.assertEqual(ram["registers_parent_bus"][0]["address"], "0x8a800000")
        self.assertNotIn("DO-NOT-EXPORT", str(report))
        self.assertIn("no live modem", report["evidence"])

    def test_truncation_and_bad_references(self):
        blob = fixture()
        for end in range(len(blob)):
            with self.assertRaises(ValueError):
                parse_fdt(blob[:end])
        with self.assertRaisesRegex(ValueError, "unresolved"):
            inventory(fixture(ref=99))
        with self.assertRaisesRegex(ValueError, "duplicate"):
            inventory(fixture(extra=[("reg", u32(0, 0))]))
        with self.assertRaisesRegex(ValueError, "duplicate phandle"):
            inventory(fixture(extra=[("phandle", u32(1))]))

    def test_invalid_bounds_tokens_and_strings(self):
        for pos, value in ((0, 0), (8, 0xffffffff), (12, 0xffffffff),
                           (32, 0xffffffff), (36, 0xffffffff), (56, 0xdeadbeef)):
            blob = bytearray(fixture()); struct.pack_into(">I", blob, pos, value)
            with self.assertRaises(ValueError):
                parse_fdt(blob)
        blob = bytearray(fixture()); blob[-1] = ord("x")
        with self.assertRaises(ValueError):
            parse_fdt(blob)

    def test_mutations_stay_bounded(self):
        rng = random.Random(70)
        original = fixture()
        for _ in range(3000):
            blob = bytearray(original)
            pos = rng.randrange(len(blob)); blob[pos] ^= 1 << rng.randrange(8)
            try:
                parse_fdt(blob)
            except ValueError:
                pass


if __name__ == "__main__":
    unittest.main()
