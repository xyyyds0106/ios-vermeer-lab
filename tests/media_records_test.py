import os
from pathlib import Path
import random
import subprocess
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from media_records import parse_record, parse_trace


def line(seq=1, status=1, valid=0, data=None, failed=0xffffffff):
    values = data if data is not None else ["--------"] * 16
    return (f"VMT1 seq={seq:016x} start=0000000000000064 end=0000000000000065 "
            f"status={status:08x} proof=00000000 req=000000ff valid={valid:08x} "
            f"fail={failed:08x} uart=skipped data=" + ",".join(values))


class RecordTests(unittest.TestCase):
    def test_missing_and_zero(self):
        r = parse_record(line())
        self.assertEqual(r["status"], "gated")
        self.assertIsNone(r["values"]["strap_cfg_a"])
        raw = line(status=0, valid=255, data=["00000000"] * 8 + ["--------"] * 8)
        r = parse_record(raw.replace("proof=00000000", "proof=0000001f"))
        self.assertEqual(r["values"]["strap_cfg_a"], 0)
        self.assertIsNone(r["values"]["camera0_reset_cfg"])

    def test_failed_index(self):
        r = parse_record(line(status=3, valid=1, failed=1,
                              data=["00000000"] + ["--------"] * 15))
        self.assertEqual(r["failed_index"], 1)
        for bad in (line(status=3), line(status=1, failed=0),
                    line(status=3, failed=16), line(status=0)):
            with self.assertRaises(ValueError):
                parse_record(bad)

    def test_replay_and_mixed_session(self):
        result = parse_trace("ordinary CDC line\n" + line() + "\n" + line() + "\n" + line(seq=2))
        self.assertEqual(len(result["records"]), 2)
        self.assertEqual(result["identical_replays"], 1)
        self.assertFalse(result["hardware_identity_verified"])
        with self.assertRaisesRegex(ValueError, "conflicting"):
            parse_trace(line() + "\n" + line().replace("0065", "0066"))
        with self.assertRaisesRegex(ValueError, "backwards"):
            parse_trace(line(seq=2) + "\n" + line(seq=1))

    def test_bounds_and_mutations(self):
        source = line()
        for n in range(len(source)):
            with self.assertRaises(ValueError):
                parse_record(source[:n])
        for bad in (source + "X", line(seq=0), source.replace("uart=skipped", "uart=ready"),
                    source.replace("valid=00000000", "valid=00000001"), "X" * 512):
            with self.assertRaises(ValueError):
                parse_record(bad)
        rng = random.Random(8550)
        for _ in range(2000):
            offset = rng.randrange(len(source))
            changed = source[:offset] + chr(rng.randrange(32, 127)) + source[offset + 1:]
            try:
                parse_record(changed)
            except ValueError:
                pass

    def test_real_c_encoder(self):
        executable = os.environ.get("VMT_TEST_EXECUTABLE")
        if not executable:
            self.skipTest("run through run-media-tests.sh for the C encoder fixture")
        text = subprocess.check_output([executable, "--fixture"], text=True)
        r = parse_trace(text)["records"][0]
        self.assertEqual(r["valid"], 0xffff)
        self.assertEqual(r["values"]["camera1_reset_io"], 0x0f178004)
        self.assertEqual(r["uart"], "skipped")


if __name__ == "__main__":
    unittest.main()
