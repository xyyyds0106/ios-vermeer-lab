import importlib.util
from pathlib import Path
import struct
import sys
import unittest
import zlib

spec = importlib.util.spec_from_file_location("records", Path(__file__).resolve().parents[1] / "tools/baseband_records.py")
records = importlib.util.module_from_spec(spec)
spec.loader.exec_module(records)
RUN = bytes(range(1, 17)).hex()
SHA = bytes(range(33, 65)).hex()


def frame(seq, state, phase=0, address=0, length=0, result=0, esr=0, far=0, vector=0, flags=0):
    p = bytearray(128)
    p[:8] = b"VBBR\x01\x00\x80\x00"
    struct.pack_into("<Q", p, 8, seq)
    p[16:32] = bytes.fromhex(RUN)
    p[32:64] = bytes.fromhex(SHA)
    struct.pack_into("<IIQIIQQII", p, 64, state, phase, address, length, result, esr, far, vector, flags)
    struct.pack_into("<I", p, 124, zlib.crc32(p[:124]))
    return "VBBR1 " + p.hex() + "\n"


def good():
    lines = [frame(1, 1)]
    for phase, (addr, size) in enumerate(((0x81eff000, 32), (0x81eff020, 96),
                                         (0x81eff000, 128), (0x81d01000, 32),
                                         (0x81d01000, 32)), 1):
        lines += [frame(len(lines)+1, 2, phase, addr, size), frame(len(lines)+2, 3, phase, addr, size)]
    lines += [frame(12, 4, 5)]
    return lines


class RecordTests(unittest.TestCase):
    def test_success_is_metadata_only(self):
        report = records.trace(good(), RUN, SHA)
        self.assertTrue(report["metadata_pass"])
        self.assertFalse(report["modem_operation_proven"])

    def test_every_partial_trace_is_incomplete(self):
        for i in range(len(good())):
            report = records.trace(good()[:i], RUN, SHA)
            self.assertFalse(report["metadata_pass"])
            self.assertIn(report["outcome"], ("incomplete", "unknown_no_records"))

    def test_missing_duplicate_reordered_and_mixed(self):
        for i in range(len(good())):
            with self.assertRaises(ValueError):
                records.trace(good()[:i]+good()[i+1:], RUN, SHA) if i < 11 else records.trace(good()+[good()[-1]], RUN, SHA)
        with self.assertRaises(ValueError): records.trace(good(), "01"*16, SHA)
        with self.assertRaises(ValueError): records.trace(good(), RUN, "01"*32)
        with self.assertRaises(ValueError): records.trace([good()[0], good()[0]], RUN, SHA)

    def test_abort_and_refusal(self):
        for phase in range(1, 6):
            lines = good()[:phase*2]
            r = records.decode(bytes.fromhex(lines[-1][6:]))
            lines += [frame(len(lines)+1, 5, phase, r["address"], r["length"],
                            esr=0x96000010, far=r["address"], vector=4, flags=3)]
            report = records.trace(lines, RUN, SHA)
            self.assertEqual(report["outcome"], "abort")
            self.assertFalse(report["metadata_pass"])
        self.assertEqual(records.trace([frame(1, 1), frame(2, 6, result=3)], RUN, SHA)["outcome"], "refused")

    def test_ranges_and_completion(self):
        cases = [[frame(1, 1), frame(2, 2, 1, 0x81db1000, 32)],
                 [frame(1, 1), frame(2, 4, 5)],
                 [frame(1, 1), frame(2, 2, 1, 0x81eff000, 64)],
                 good()[:7]+[frame(8, 2, 4, 0x81eff000, 32)],
                 good()[:9]+[frame(10, 2, 5, 0x81d02000, 32)],
                 good()[:5]+[frame(6, 2, 3, 0x81eff000, 32)]]
        for lines in cases:
            with self.assertRaises(ValueError): records.trace(lines, RUN, SHA)

    def test_crc_reserved_size_and_noise(self):
        raw = bytes.fromhex(frame(1, 1)[6:])
        for i in range(128):
            for bit in range(8):
                altered = bytearray(raw);altered[i] ^= 1 << bit
                with self.assertRaises(ValueError): records.decode(altered)
        for i in range(128):
            with self.assertRaises(ValueError): records.decode(raw[:i])
        altered = bytearray(raw);altered[112] = 1
        struct.pack_into("<I", altered, 124, zlib.crc32(altered[:124]))
        with self.assertRaises(ValueError): records.decode(altered)
        self.assertTrue(records.trace(["other log\n"]+good(), RUN, SHA)["metadata_pass"])
        with self.assertRaises(ValueError): records.trace(["VBBR1 broken\n"], RUN, SHA)


if __name__ == "__main__":
    if len(sys.argv) == 2:
        report = records.trace(Path(sys.argv.pop()).read_text().splitlines(), RUN, SHA)
        assert report["outcome"] == "abort" and report["last"]["esr"] == 0x96000010
    unittest.main()
