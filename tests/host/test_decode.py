#!/usr/bin/env python3

import importlib.util
import contextlib
import io
from pathlib import Path
import struct
import sys
import types
import unittest


ROOT = Path(__file__).resolve().parents[2]


class _StopImport(Exception):
    pass


class _SerialStub:
    in_waiting = 0

    def __init__(self, *_args, **_kwargs):
        pass

    def read(self, _size):
        raise _StopImport


serial_stub = types.ModuleType("serial")
serial_stub.Serial = _SerialStub
sys.modules.setdefault("serial", serial_stub)
SPEC = importlib.util.spec_from_file_location("dreo_decode", ROOT / "protocol" / "decode.py")
decode = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = decode
try:
    SPEC.loader.exec_module(decode)
except _StopImport:
    pass

FIXED = "--fixed" in sys.argv


def integer_dp(value: bytes) -> bytes:
    return bytes((1, 1, 2)) + len(value).to_bytes(2, "big") + value


def frame(sequence: int, command: int, body: bytes = b"") -> bytes:
    packet = b"\x55\xaa" + bytes((0, sequence, command, 0)) + len(body).to_bytes(2, "big") + body
    return packet + bytes((sum(packet) & 0xFF,))


def decode_output(data: bytes) -> tuple[bytes, str]:
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        remainder = decode.decode_datastream("test", data)
    return remainder, output.getvalue()


@unittest.skipIf(FIXED, "unmodified-source assertions")
class DecoderBaselineTest(unittest.TestCase):
    def test_one_and_four_byte_integer_positive_controls(self):
        self.assertEqual(decode.parse_dp(integer_dp(b"\x80"))[0].value, -128)
        self.assertEqual(
            decode.parse_dp(integer_dp(b"\x80\x00\x00\x00"))[0].value,
            -(2**31),
        )

    def test_two_byte_integer_is_not_supported_yet(self):
        with self.assertRaises(struct.error):
            decode.parse_dp(integer_dp(b"\x80\x00"))


@unittest.skipUnless(FIXED, "fixed-source assertions")
class DecoderFixedTest(unittest.TestCase):
    def test_signed_integer_boundaries(self):
        cases = (
            (b"\x80", -(2**7)),
            (b"\x00", 0),
            (b"\x7f", 2**7 - 1),
            (b"\x80\x00", -(2**15)),
            (b"\x00\x00", 0),
            (b"\x7f\xff", 2**15 - 1),
            (b"\x80\x00\x00\x00", -(2**31)),
            (b"\x00\x00\x00\x00", 0),
            (b"\x7f\xff\xff\xff", 2**31 - 1),
        )
        for encoded, expected in cases:
            with self.subTest(encoded=encoded.hex()):
                self.assertEqual(decode.parse_dp(integer_dp(encoded))[0].value, expected)

    def test_other_integer_widths_reject_deliberately(self):
        for width in (0, 3, 5):
            with self.subTest(width=width), self.assertRaises(ValueError):
                decode.parse_dp(integer_dp(bytes(width)))

    def test_multi_item_change_and_typed_session_commands(self):
        body = b"".join(integer_dp(bytes((value,))) for value in range(10))
        remainder, output = decode_output(
            frame(1, 0x06, body) + frame(2, 0x0E, bytes((2, 4, 7))) + frame(3, 0x10)
        )
        self.assertEqual(remainder, b"")
        self.assertEqual(output.count("dpId 1"), 10)
        self.assertIn("origin=2, duration_seconds=4, button_id=7", output)
        self.assertIn("Module reset request/ack", output)

    def test_stream_recovers_after_abandoned_and_corrupt_candidates(self):
        good = frame(9, 0x0E, bytes((1, 1, 3)))
        abandoned = b"\x55\xaa\x00\x08\x07\x00\x01\x00\x01\x01"
        corrupt = bytearray(frame(10, 0x07, integer_dp(b"\x01")))
        corrupt[-1] ^= 0xFF
        remainder, output = decode_output(abandoned + good + bytes(corrupt) + good)
        self.assertEqual(remainder, b"")
        self.assertEqual(output.count("origin=1, duration_seconds=1, button_id=3"), 2)
        self.assertIn("abandoned incomplete frame", output)
        self.assertIn("Checksum mismatch", output)

    def test_missing_leading_magic_is_not_reconstructed(self):
        missing_lead = frame(4, 0x0E, bytes((1, 1, 5)))[1:]
        good = frame(5, 0x0E, bytes((1, 1, 6)))
        remainder, output = decode_output(missing_lead + good)
        self.assertEqual(remainder, b"")
        self.assertNotIn("button_id=5", output)
        self.assertIn("button_id=6", output)

    def test_valid_body_magic_is_not_resynchronized(self):
        body = bytes((24, 1, 3, 0, 4)) + b"U\xaaok"
        remainder, output = decode_output(frame(6, 0x07, body))
        self.assertEqual(remainder, b"")
        self.assertIn("55aa6f6b", output)


if __name__ == "__main__":
    if "--fixed" in sys.argv:
        sys.argv.remove("--fixed")
    unittest.main()
