import io
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import baseband_observer as observer
import baseband_records as records

RUN = "01" + "00"*15
SHA = "02" + "00"*31
BINARY = None


def frame(seq=1, state=1, phase=0, addr=0, length=0, result=0):
    raw = bytearray(128)
    raw[:8] = b"VBDR\x01\x00\x80\x00"
    struct.pack_into("<Q", raw, 8, seq)
    raw[16:32], raw[32:64] = bytes.fromhex(RUN), bytes.fromhex(SHA)
    struct.pack_into("<IIQII", raw, 64, state, phase, addr, length, result)
    struct.pack_into("<I", raw, 124, zlib.crc32(raw[:124]))
    return b"\nVBDR1 " + raw.hex().encode() + b"\n"


class Sink(io.BytesIO):
    def __init__(self, failure=None):
        super().__init__();self.events = [];self.failure = failure

    def write(self, data):
        self.events.append("write")
        if self.failure == "write": raise OSError("write failed")
        if self.failure == "short": return super().write(data[:-1])
        return super().write(data)

    def flush(self):
        self.events.append("flush")
        if self.failure == "flush": raise OSError("flush failed")
        return super().flush()

    def fileno(self): return 42


class ObserverTests(unittest.TestCase):
    def test_every_split_and_byte_delivery(self):
        line = frame()
        for split in range(len(line)+1):
            sink = Sink();receiver = observer.DryReceiver(sink, RUN, SHA)
            with mock.patch.object(observer.os, "fsync", side_effect=lambda _: sink.events.append("fsync")):
                ack = receiver.feed(line[:split]) + receiver.feed(line[split:])
            self.assertEqual(len(ack), 1)
            self.assertEqual(sink.events, ["write", "flush", "fsync"])
            self.assertEqual(sink.getvalue(), line[1:])
            self.assertEqual(len(ack[0]), 62)
        sink = Sink();receiver = observer.DryReceiver(sink, RUN, SHA)
        with mock.patch.object(observer.os, "fsync"):
            for byte in line[:-1]: self.assertEqual(receiver.feed(bytes([byte])), [])
            self.assertEqual(len(receiver.feed(line[-1:])), 1)

    def test_durable_failure_never_acknowledges(self):
        for failure in ("write", "short", "flush", "fsync"):
            sink = Sink(failure);receiver = observer.DryReceiver(sink, RUN, SHA)
            with mock.patch.object(observer.os, "fsync", side_effect=OSError("flush failed") if failure == "fsync" else None):
                with self.assertRaises(OSError): receiver.feed(frame())
            self.assertTrue(receiver.failed)
            self.assertEqual(receiver.lines, [])
            with self.assertRaises(ValueError): receiver.feed(frame())

    def test_corruption_noise_and_identity(self):
        raw = bytes.fromhex(frame()[7:-1].decode())
        with self.assertRaises(ValueError): records.decode(raw)
        self.assertFalse(records.trace(frame().decode().splitlines(), RUN, SHA)["metadata_pass"])
        for bad in (frame()[:100]+b"CPUinterleave"+frame()[100:], frame()[:-3]+b"00\n"):
            receiver = observer.DryReceiver(Sink(), RUN, SHA)
            with self.assertRaises(ValueError): receiver.feed(bad)
            self.assertEqual(receiver.lines, [])
        receiver = observer.DryReceiver(Sink(), "03"+"00"*15, SHA)
        with self.assertRaises(ValueError): receiver.feed(frame())
        receiver = observer.DryReceiver(Sink(), RUN, SHA)
        with mock.patch.object(observer.os, "fsync"):
            self.assertEqual(len(receiver.feed(b"unfinished console text"+frame())), 1)
            with self.assertRaises(ValueError): receiver.feed(frame())
        receiver = observer.DryReceiver(Sink(), RUN, SHA)
        with self.assertRaises(ValueError): receiver.feed(b"x"*4097)

    def test_pending_ack_refusal_without_read_result(self):
        sink = Sink();receiver = observer.DryReceiver(sink, RUN, SHA)
        with mock.patch.object(observer.os, "fsync"):
            for line in (frame(), frame(2, 2, 1, 0x81eff000, 32), frame(3, 6, 1, result=3)):
                self.assertEqual(len(receiver.feed(line)), 1)
        result = receiver.result()
        self.assertEqual(result["outcome"], "refused")
        self.assertFalse(result["metadata_pass"])
        self.assertFalse(result["synthetic_pass"])
        saved_states = [observer.dry_decode(line)[0]["state"]
                        for line in sink.getvalue().splitlines(keepends=True)]
        self.assertEqual(saved_states, [1, 2, 6])
        for bad in (frame(3, 6, 2, result=3), frame(3, 6, 1, result=1)):
            fresh = observer.DryReceiver(Sink(), RUN, SHA)
            with mock.patch.object(observer.os, "fsync"):
                fresh.feed(frame());fresh.feed(frame(2, 2, 1, 0x81eff000, 32))
                with self.assertRaises(ValueError): fresh.feed(bad)

    def test_real_journal_is_exclusive(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/"run.log"
            with observer.create_journal(path) as sink:
                receiver = observer.DryReceiver(sink, RUN, SHA)
                self.assertEqual(len(receiver.feed(frame())), 1)
                self.assertEqual(path.read_bytes(), frame()[1:])
            with self.assertRaises(FileExistsError): observer.create_journal(path)

    def interaction(self, scenario):
        """Actual C worker + fragmented RX + actual host file/fsync + ACK input."""
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/"run.log"
            child = subprocess.Popen([BINARY, "--interactive"], stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                with observer.create_journal(path) as sink:
                    receiver = observer.DryReceiver(sink, RUN, SHA)
                    end = None
                    while True:
                        line = child.stdout.readline()
                        if not line: self.fail("C worker ended without status")
                        if line.startswith(b"OBSERVER END"):
                            end = line.decode().strip();break
                        # Feed one byte at a time. No ACK before newline + fsync.
                        acks = []
                        for byte in line: acks += receiver.feed(bytes([byte]))
                        for ack in acks:
                            saved = path.read_bytes()
                            self.assertTrue(saved.endswith(line))
                            self.assertEqual(len(saved), 263*len(receiver.lines))
                            self.assertEqual(observer.dry_decode(saved[-263:])[0]["sequence"],
                                             receiver.result()["last"]["sequence"])
                            state = receiver.result()["last"]["state"]
                            if state == 2 and scenario != "success":
                                if scenario == "disconnect": command = b"disconnect\n"
                                elif scenario == "wrong":
                                    for pos in (3, 36, 53):
                                        bad = bytearray(ack);bad[pos] = ord('f') if bad[pos] != ord('f') else ord('e')
                                        child.stdin.write(bad);child.stdin.flush()
                                    command = b"timeout\n"
                                else: command = b"timeout\n" # host saved; ACK deliberately lost
                            else: command = ack
                            child.stdin.write(command);child.stdin.flush()
                child.stdin.close()
                self.assertEqual(child.wait(timeout=5), 0, child.stderr.read().decode())
                result = receiver.result()
                self.assertFalse(result["metadata_pass"])
                self.assertFalse(result["modem_operation_proven"])
                if scenario == "success":
                    self.assertTrue(result["synthetic_pass"])
                    self.assertEqual(end, "OBSERVER END state=3 steps=5 error=0")
                elif scenario == "disconnect":
                    self.assertEqual(result["outcome"], "incomplete")
                    self.assertEqual(end, "OBSERVER END state=4 steps=0 error=3")
                else:
                    self.assertEqual(result["outcome"], "refused")
                    self.assertEqual(end, "OBSERVER END state=4 steps=0 error=1")
            finally:
                if child.poll() is None: child.kill();child.wait(timeout=5)
                for stream in (child.stdin, child.stdout, child.stderr): stream.close()

    def test_c_host_success(self): self.interaction("success")
    def test_c_host_persisted_but_ack_lost(self): self.interaction("lost")
    def test_c_host_wrong_ack_token_sequence_crc(self): self.interaction("wrong")
    def test_c_host_disconnect(self): self.interaction("disconnect")

    def test_c_host_fsync_failure_before_step(self):
        with tempfile.TemporaryDirectory() as folder:
            child = subprocess.Popen([BINARY, "--interactive"], stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            def next_frame():
                self.assertEqual(child.stdout.readline(), b"\n")
                return child.stdout.readline()
            try:
                with observer.create_journal(Path(folder)/"run.log") as sink:
                    receiver = observer.DryReceiver(sink, RUN, SHA)
                    ack = receiver.feed(next_frame())[0]
                    child.stdin.write(ack);child.stdin.flush()
                    before = next_frame()
                    with mock.patch.object(observer.os, "fsync", side_effect=OSError("disk flush failed")):
                        with self.assertRaises(OSError): receiver.feed(before)
                    self.assertTrue(receiver.failed)
                    self.assertEqual(len(receiver.lines), 1)
                    child.stdin.write(b"timeout\n");child.stdin.flush()
                    terminal = observer.dry_decode(next_frame())[0]
                    self.assertEqual((terminal["state"], terminal["result"]), (6, 3))
                    child.stdin.write(b"timeout\n");child.stdin.flush()
                    self.assertEqual(child.stdout.readline(), b"OBSERVER END state=4 steps=0 error=1\n")
                child.stdin.close();self.assertEqual(child.wait(timeout=5), 0)
            finally:
                if child.poll() is None: child.kill();child.wait(timeout=5)
                for stream in (child.stdin, child.stdout, child.stderr): stream.close()


if __name__ == "__main__":
    BINARY = sys.argv.pop(1)
    unittest.main()
