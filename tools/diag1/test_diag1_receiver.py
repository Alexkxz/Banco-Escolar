import struct
import tempfile
import time
import unittest
import zlib
from pathlib import Path

from diag1_receiver import (
    DATA, END, ERROR, META, RGB565_LE, STATUS, CaptureError, TransferTimeout,
    encode_frame, read_frame, receive_capture,
)


class FakeSerial:
    def __init__(self, data=b""):
        self.data = bytearray(data)
        self.writes = []

    def read(self, count):
        if not self.data:
            return b""
        result = bytes(self.data[:count])
        del self.data[:count]
        return result

    def write(self, data):
        self.writes.append(bytes(data))
        return len(data)

    def flush(self):
        pass


class DisconnectedSerial(FakeSerial):
    def read(self, count):
        raise OSError("synthetic USB disconnect")


class DelayedAckSerial(FakeSerial):
    def __init__(self, data=b"", delay=0.02):
        super().__init__(data)
        self.delay = delay
        self.delayed_ack = False

    def write(self, data):
        if data.startswith(b"DIAG1 ACK ") and not self.delayed_ack:
            time.sleep(self.delay)
            self.delayed_ack = True
        return super().write(data)


def fixture_frames(*, bad_checksum=False, duplicate_chunk=False, busy=False, large=False):
    capture_id = 0x1A2B3C4D
    image = (bytes((index * 7) & 0xFF for index in range(1040)) if large
             else b"\x00\xF8\xE0\x07")  # synthetic RGB565 image
    width, height = (40, 13) if large else (2, 1)
    image_crc = __import__("zlib").crc32(image) & 0xFFFFFFFF
    meta = encode_frame(META, capture_id, struct.pack("<II", len(image), image_crc),
                        width=width, height=height, pixel_format=RGB565_LE)
    frames = [b"[Monitor] boot ready\r\n"]
    if busy:
        frames.append(encode_frame(ERROR, 0, b"BUSY: solicitud de captura en curso"))
    chunks = [image[offset:offset + 1024] for offset in range(0, len(image), 1024)]
    chunk_frames = [encode_frame(DATA, capture_id, chunk, width=width, height=height,
                                 pixel_format=RGB565_LE, sequence=sequence)
                    for sequence, chunk in enumerate(chunks)]
    frames.append(meta)
    if bad_checksum:
        damaged = bytearray(chunk_frames[0])
        damaged[-1] ^= 0x01
        frames.append(bytes(damaged))
    frames.append(chunk_frames[0])
    if duplicate_chunk:
        frames.append(chunk_frames[0])
    frames.extend(chunk_frames[1:])
    summary = struct.pack("<II", len(image), image_crc)
    frames.extend((encode_frame(END, capture_id, summary, width=width, height=height,
                               pixel_format=RGB565_LE),
                   encode_frame(STATUS, capture_id, b"SD no disponible")))
    return b"".join(frames)


class ReceiverTests(unittest.TestCase):
    def test_valid_synthetic_transfer_saves_bmp(self):
        port = FakeSerial(fixture_frames())
        with tempfile.TemporaryDirectory() as temp:
            path = receive_capture(port, "listen", Path(temp), timeout=0.1)
            self.assertEqual(path.read_bytes()[:2], b"BM")
            self.assertEqual(path.stat().st_size, 62)
            checksum = zlib.crc32(b"\x00\xF8\xE0\x07") & 0xFFFFFFFF
            self.assertEqual(port.writes, [
                b"DIAG1 ACK 1A2B3C4D 0\n",
                f"DIAG1 VERIFIED 1A2B3C4D {checksum:08X}\n".encode("ascii"),
            ])

    def test_truncated_frame_is_rejected_without_saving(self):
        port = FakeSerial(encode_frame(META, 1, struct.pack("<II", 4, 0),
                                       width=2, height=1, pixel_format=RGB565_LE) +
                          b"BED1\x01\x02")
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(TransferTimeout):
                receive_capture(port, "listen", Path(temp), timeout=0.1)
            self.assertEqual(list(Path(temp).glob("*.bmp")), [])
            self.assertEqual(list(Path(temp).glob("*.part")), [])

    def test_bad_chunk_checksum_is_nacked_then_valid_retry_is_saved(self):
        port = FakeSerial(fixture_frames(bad_checksum=True))
        with tempfile.TemporaryDirectory() as temp:
            path = receive_capture(port, "listen", Path(temp), timeout=0.1)
            self.assertTrue(path.exists())
            self.assertEqual(port.writes, [
                b"DIAG1 NAK 1A2B3C4D 0\n",
                b"DIAG1 ACK 1A2B3C4D 0\n",
                f"DIAG1 VERIFIED 1A2B3C4D {zlib.crc32(bytes((0, 248, 224, 7))):08X}\n".encode("ascii"),
            ])

    def test_end_checksum_mismatch_is_rejected_without_publishing_image(self):
        stream = fixture_frames()
        # Corrupt the terminal image checksum in END while keeping the frame
        # checksum valid, so the receiver must reject the complete transfer.
        image = b"\x00\xF8\xE0\x07"
        bad_summary = struct.pack("<II", len(image), 0)
        bad_end = encode_frame(END, 0x1A2B3C4D, bad_summary,
                               width=2, height=1, pixel_format=RGB565_LE)
        meta = encode_frame(META, 0x1A2B3C4D,
                            struct.pack("<II", len(image), __import__("zlib").crc32(image) & 0xFFFFFFFF),
                            width=2, height=1, pixel_format=RGB565_LE)
        chunk = encode_frame(DATA, 0x1A2B3C4D, image,
                             width=2, height=1, pixel_format=RGB565_LE, sequence=0)
        del stream
        port = FakeSerial(meta + chunk + bad_end)
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(CaptureError):
                receive_capture(port, "listen", Path(temp), timeout=0.1)
            self.assertEqual(list(Path(temp).glob("*.bmp")), [])
            self.assertEqual(list(Path(temp).glob("*.part")), [])
            self.assertFalse(any(b"DIAG1 VERIFIED" in write for write in port.writes))

    def test_timeout_waiting_for_capture(self):
        with self.assertRaises(TransferTimeout):
            read_frame(FakeSerial(), timeout=0.01)

    def test_usb_disconnect_does_not_publish_image(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(OSError):
                receive_capture(DisconnectedSerial(), "listen", Path(temp), timeout=0.1)
            self.assertEqual(list(Path(temp).glob("*.bmp")), [])
            self.assertEqual(list(Path(temp).glob("*.part")), [])

    def test_repeated_request_busy_and_duplicate_chunk_do_not_duplicate_pixels(self):
        port = FakeSerial(fixture_frames(duplicate_chunk=True, busy=True, large=True))
        with tempfile.TemporaryDirectory() as temp:
            path = receive_capture(port, "request", Path(temp), timeout=0.2)
            self.assertTrue(path.exists())
            acks = [write for write in port.writes if b"DIAG1 ACK" in write]
            self.assertEqual(acks, [
                b"DIAG1 ACK 1A2B3C4D 0\n",  # original block
                b"DIAG1 ACK 1A2B3C4D 0\n",  # retry of the same block
                b"DIAG1 ACK 1A2B3C4D 1\n",
            ])
            self.assertEqual(len(list(Path(temp).glob("*.bmp"))), 1)

    def test_late_retry_of_same_block_gets_a_second_matching_ack(self):
        # The duplicate represents a block retransmitted after the first ACK
        # arrived late or was lost; the host must acknowledge it again.
        port = DelayedAckSerial(fixture_frames(duplicate_chunk=True, large=True))
        with tempfile.TemporaryDirectory() as temp:
            path = receive_capture(port, "listen", Path(temp), timeout=0.1)
            self.assertTrue(path.exists())
            self.assertEqual(port.writes[:3], [
                b"DIAG1 ACK 1A2B3C4D 0\n",
                b"DIAG1 ACK 1A2B3C4D 0\n",
                b"DIAG1 ACK 1A2B3C4D 1\n",
            ])
            self.assertTrue(port.writes[3].startswith(b"DIAG1 VERIFIED 1A2B3C4D "))
            self.assertTrue(port.delayed_ack)

    def test_firmware_ack_failure_diagnostic_is_preserved(self):
        diagnostic = (b"ACK_TIMEOUT_OR_RETRY_LIMIT id=1A2B3C4D seq=17 "
                      b"retries=3 cause=TIMEOUT reply=ACK reply_id=1A2B3C4D "
                      b"reply_seq=16 match=0 reply_attempt=3 reply_wait_ms=42 "
                      b"reply_block_ms=10042 final_wait_ms=5000 block_ms=25042")
        stream = encode_frame(META, 0x1A2B3C4D, struct.pack("<II", 4, 0),
                              width=2, height=1, pixel_format=RGB565_LE)
        stream += encode_frame(ERROR, 0x1A2B3C4D, diagnostic)
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaisesRegex(CaptureError, "seq=17.*reply_seq=16.*block_ms=25042"):
                receive_capture(FakeSerial(stream), "listen", Path(temp), timeout=0.1)
            self.assertEqual(list(Path(temp).glob("*.bmp")), [])

    def test_request_retries_are_limited(self):
        from diag1_receiver import MAX_REQUEST_ATTEMPTS, wait_for_meta
        port = FakeSerial()
        with self.assertRaises(TransferTimeout):
            wait_for_meta(port, "request", timeout=0.04, request_interval=0.001)
        self.assertLessEqual(len(port.writes), MAX_REQUEST_ATTEMPTS)

    def test_repeated_capture_names_never_overwrite(self):
        port = FakeSerial(fixture_frames())
        with tempfile.TemporaryDirectory() as temp:
            first = receive_capture(port, "listen", Path(temp), timeout=0.1)
            second = receive_capture(FakeSerial(fixture_frames()), "listen", Path(temp), timeout=0.1)
            self.assertNotEqual(first, second)
            self.assertTrue(first.exists() and second.exists())


if __name__ == "__main__":
    unittest.main()
