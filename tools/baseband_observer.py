"""Dry-run record reassembly and durable host acknowledgement. No device I/O.

An integration owner feeds received bytes and sends only the returned ACKs.
Synthetic VBDR frames can never report real metadata/modem success.
"""
import os
from pathlib import Path
import struct
import zlib

from baseband_records import decode, trace

PREFIX = b"VBDR1 "


def dry_decode(line):
    if len(line) != 263 or not line.startswith(PREFIX) or not line.endswith(b"\n"):
        raise ValueError("dry record framing")
    raw = bytes.fromhex(line[6:-1].decode("ascii"))
    if len(raw) != 128 or raw[:8] != b"VBDR\x01\x00\x80\x00":
        raise ValueError("dry record magic/version")
    crc = struct.unpack_from("<I", raw, 124)[0]
    if crc != zlib.crc32(raw[:124]):
        raise ValueError("dry record CRC")
    normalized = bytearray(raw)
    normalized[:4] = b"VBBR"
    struct.pack_into("<I", normalized, 124, zlib.crc32(normalized[:124]))
    record = decode(normalized)
    return record, crc, "VBBR1 " + normalized.hex() + "\n"


class DryReceiver:
    """One run, one owner. sink.write/flush/fileno must have normal file semantics.

    The integration supplies an exclusively owned journal, opened before feeding
    bytes. Records are acknowledged only after complete write, flush and fsync.
    No lock/printf/serial callback is invoked. On any failure this run is poisoned.
    """
    def __init__(self, sink, run, image_sha256):
        if (len(run) != 32 or len(image_sha256) != 64 or
                not any(bytes.fromhex(run)) or not any(bytes.fromhex(image_sha256))):
            raise ValueError("run/image identity")
        self.run, self.image_sha256 = run.lower(), image_sha256.lower()
        self.sink = sink
        self.buffer = bytearray()
        self.lines = []
        self.failed = False

    def feed(self, data):
        if self.failed:
            raise ValueError("receiver is permanently stopped")
        acks = []
        try:
            for byte in data:
                if len(self.buffer) >= 4096:
                    raise ValueError("unterminated/oversized console line")
                self.buffer.append(byte)
                if byte != 10:
                    continue
                line = bytes(self.buffer)
                self.buffer.clear()
                if not line.startswith(PREFIX):
                    continue
                r, crc, normalized = dry_decode(line)
                if len(self.lines) >= 13:
                    raise ValueError("record limit")
                # Validate before saving; commit receiver state only after fsync.
                trace(self.lines + [normalized], self.run, self.image_sha256)
                if self.sink.write(line) != len(line):
                    raise OSError("short journal write")
                self.sink.flush()
                os.fsync(self.sink.fileno())
                self.lines.append(normalized)
                ack = f"ba {self.run} {r['sequence']:016x} {crc:08x}\n".encode("ascii")
                if len(ack) != 62:
                    raise ValueError("ACK size")
                acks.append(ack)
        except (OSError, ValueError, UnicodeError, TypeError):
            self.failed = True
            raise
        return acks

    def result(self):
        result = trace(self.lines, self.run, self.image_sha256)
        result["synthetic_pass"] = result["metadata_pass"] and not self.failed and not self.buffer
        result["metadata_pass"] = False
        result["dry_run"] = True
        result["receiver_failed"] = self.failed
        result["pending_line_bytes"] = len(self.buffer)
        return result


def create_journal(path):
    """Exclusively create a host file; never overwrite an earlier run.

    Flush file creation before returning. POSIX also fsyncs the directory; on
    Windows the caller owns directory setup. No claim is made about a disk
    ignoring flush requests or power-loss durability of a new Windows directory.
    """
    path = Path(path)
    stream = path.open("xb", buffering=0)
    try:
        os.fsync(stream.fileno())
        if os.name == "posix":
            descriptor = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(descriptor)
            finally:
                os.close(descriptor)
    except OSError:
        stream.close()
        raise
    return stream
