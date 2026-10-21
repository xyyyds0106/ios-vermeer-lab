from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from baseband_start_commands import build_start_commands

TOKEN = "01" + "00"*15
DIGEST = "02" + "00"*31
BINARY = None


class StartCommandsTests(unittest.TestCase):
    def run_commands(self, commands):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/"commands.txt"
            path.write_bytes(b"".join(commands))
            result = subprocess.run([BINARY, "--script", str(path)], timeout=10,
                                    capture_output=True, check=True, text=True)
        return {k: int(v) for k, v in (word.split("=") for word in result.stdout.split())}

    def test_builder_actual_c_observer_five_steps(self):
        commands = build_start_commands(TOKEN, DIGEST)
        self.assertIsInstance(commands, tuple)
        self.assertEqual([len(c)-1 for c in commands], [35, 53, 53, 53, 53, 35])
        self.assertEqual(commands[1], f"bh0 {DIGEST[:16]} {TOKEN}\n".encode())
        result = self.run_commands(commands)
        self.assertEqual(result, dict(starts=1, steps=5, stage=2, observer=3, error=0))

    def test_builder_identity_validation(self):
        for token, digest in (("0"*32, DIGEST), (TOKEN, "0"*64), (TOKEN.upper().replace("1", "A"), DIGEST),
                              (TOKEN+" ", DIGEST), (TOKEN, "g"*64), (None, DIGEST)):
            with self.assertRaises(ValueError): build_start_commands(token, digest)

    def test_invalid_sequences_never_start(self):
        good = list(build_start_commands(TOKEN, DIGEST))
        cases = [good[1:], good[:1]+good[2:], good[:2]+good[1:],
                 good[:1]+[good[2], good[1]]+good[3:], good[:2]+[b"timeout\n"]+good[2:],
                 good[:5]+[b"disconnect\n"]+[good[5]],
                 good[:1]+[good[1].replace(TOKEN.encode(), b"f"*32)]+good[2:],
                 good[:1]+[b"bh0 "+b"0"*16+b" "+TOKEN.encode()+b"\n"]+good[2:],
                 [good[0], good[0]]+good[1:]]
        for index, commands in enumerate(cases):
            with self.subTest(index=index):
                result = self.run_commands(commands)
                self.assertEqual((result["starts"], result["steps"]), (0, 0))

    def test_each_truncated_or_invalid_line_never_starts(self):
        good = list(build_start_commands(TOKEN, DIGEST))
        for index, line in enumerate(good):
            for altered in (line[:-2]+b"\n", line[:-1]+b"x\n", b"x"*64+b"\n", line.replace(b" ", b"\t", 1)):
                commands = good[:];commands[index] = altered
                result = self.run_commands(commands)
                self.assertEqual((result["starts"], result["steps"]), (0, 0))

    def test_consumed_session_cannot_restart(self):
        good = list(build_start_commands(TOKEN, DIGEST))
        for following in (good, list(build_start_commands("03"+"00"*15, DIGEST))):
            result = self.run_commands(good+following)
            self.assertEqual(result["starts"], 1)
            self.assertEqual(result["steps"], 0) # later invalid start stops before poll


if __name__ == "__main__":
    BINARY = sys.argv.pop(1)
    unittest.main()
