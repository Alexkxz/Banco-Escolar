import struct
import tempfile
import time
import unittest
import zlib
from pathlib import Path

from diag1_receiver import (
    DATA, END, ERROR, META, RGB565_LE, STATUS, CaptureError, TransferTimeout,
    collect_firmware_diagnostics, create_argument_parser, encode_frame,
    read_frame, receive_capture, write_metrics,
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


class PartialAckSerial(FakeSerial):
    def write(self, data):
        if data.startswith((b"DIAG1 ACK ", b"DIAG1 NAK ")):
            self.writes.append(bytes(data))
            return len(data) - 1
        return super().write(data)


class FirmwareLogSerial(FakeSerial):
    def __init__(self, lines):
        super().__init__()
        self.lines = list(lines)

    def readline(self):
        return self.lines.pop(0) if self.lines else b""


def fixture_frames(*, bad_checksum=False, duplicate_chunk=False, duplicate_final=False,
                   busy=False, large=False):
    capture_id = 0x1A2B3C4D
    image = (bytes((index * 7) & 0xFF for index in range(8194)) if large
             else b"\x00\xF8\xE0\x07")  # synthetic RGB565 image
    width, height = (1, 4097) if large else (2, 1)
    image_crc = __import__("zlib").crc32(image) & 0xFFFFFFFF
    meta = encode_frame(META, capture_id, struct.pack("<II", len(image), image_crc),
                        width=width, height=height, pixel_format=RGB565_LE)
    frames = [b"[Monitor] boot ready\r\n"]
    if busy:
        frames.append(encode_frame(ERROR, 0, b"BUSY: solicitud de captura en curso"))
    chunks = [image[offset:offset + 4096] for offset in range(0, len(image), 4096)]
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
    if duplicate_final:
        frames.append(chunk_frames[-1])
    summary = struct.pack("<II", len(image), image_crc)
    frames.extend((encode_frame(END, capture_id, summary, width=width, height=height,
                               pixel_format=RGB565_LE),
                   encode_frame(STATUS, capture_id, b"SD no disponible")))
    return b"".join(frames)


class ReceiverTests(unittest.TestCase):
    def test_baud_selection_and_metrics_record_receiver_configuration(self):
        parser = create_argument_parser()
        self.assertEqual(parser.parse_args([]).baud, 115200)
        self.assertIsNone(parser.parse_args([]).metrics)
        self.assertEqual(parser.parse_args(["--metrics", "attempt.json"]).metrics,
                         Path("attempt.json"))
        self.assertEqual(parser.parse_args(["--baud", "115200"]).baud, 115200)
        self.assertEqual(parser.parse_args(["--baud", "460800"]).baud, 460800)
        with self.assertRaises(SystemExit):
            parser.parse_args(["--baud", "230400"])

        metrics = {}
        with tempfile.TemporaryDirectory() as temp:
            receive_capture(FakeSerial(fixture_frames()), "listen", Path(temp),
                            timeout=0.1, metrics=metrics, baud=460800)
        self.assertEqual(metrics["receiver_configured_baud"], 460800)
        self.assertFalse(metrics["firmware_baud_confirmed"])

    def test_firmware_metrics_are_collected_separately_from_host_ack_counts(self):
        port = FirmwareLogSerial([
            b"DIAG1_FW_METRICS result=verified baud_configured=460800 blocks_sent_attempts=2 valid_acks=1 naks=1 ack_timeouts=0 retries=1 errors=0 transfer_total_ms=40\n",
            b"DIAG1_FW_BLOCK seq=0 send_ms=10 ack_wait_ms=30\n",
            b"DIAG1_FW_ATTEMPT seq=0 retry=0 polls=3 no_space=1 space_samples=2 space_avg=32 space_max=40 write_calls=2 bytes_accepted=64 write_active_us=20 poll_gap_avg_us=50000 poll_gap_max_us=50010 ack_latency_us=0\n",
            b"DIAG1_FW_ATTEMPT seq=0 retry=1 polls=2 no_space=0 space_samples=2 space_avg=24 space_max=32 write_calls=2 bytes_accepted=64 write_active_us=18 poll_gap_avg_us=50001 poll_gap_max_us=50001 ack_latency_us=30000\n",
        ])
        metrics = {"capture_id": "1A2B3C4D", "image_bytes": 4096,
                   "receiver_ack_sent": 1, "firmware_ack_received_count": None}
        collect_firmware_diagnostics(port, metrics, timeout=0.1)
        self.assertEqual(metrics["receiver_ack_sent"], 1)
        self.assertIsNone(metrics["firmware_ack_received_count"])
        self.assertEqual(metrics["firmware_metrics"]["summary"]["valid_acks"], "1")
        self.assertEqual(metrics["firmware_metrics"]["block_timings"],
                         [{"seq": 0, "send_ms": 10, "ack_wait_ms": 30}])
        self.assertEqual(metrics["firmware_metrics"]["summary"]["retries"], "1")
        self.assertEqual([(row["seq"], row["retry"], row["bytes_accepted"])
                          for row in metrics["firmware_metrics"]["attempt_timings"]],
                         [(0, 0, 64), (0, 1, 64)])
        self.assertEqual(metrics["firmware_metrics"]["attempt_timings"][0]["no_space"], 1)

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

    def test_failed_receive_records_monotonic_metrics(self):
        metrics = {}
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(TransferTimeout):
                receive_capture(FakeSerial(), "listen", Path(temp), timeout=0.01,
                                metrics=metrics)
        self.assertEqual(metrics["result"], "failed")
        self.assertIsNotNone(metrics["error"])
        self.assertGreaterEqual(metrics["initial_capture_wait_seconds"], 0)
        self.assertGreaterEqual(metrics["total_seconds"], metrics["initial_capture_wait_seconds"])

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
                b"DIAG1 ACK 1A2B3C4D 2\n",
            ])
            self.assertEqual(path.read_bytes()[:2], b"BM")
            self.assertEqual(len(list(Path(temp).glob("*.bmp"))), 1)

    def test_4096_byte_blocks_and_partial_final_block(self):
        port = FakeSerial(fixture_frames(large=True))
        metrics = {}
        with tempfile.TemporaryDirectory() as temp:
            path = receive_capture(port, "listen", Path(temp), timeout=0.1, metrics=metrics)
            self.assertTrue(path.exists())
        self.assertEqual(metrics["accepted_blocks"], 3)
        self.assertEqual(metrics["unique_bytes"], 8194)
        self.assertEqual(metrics["accepted_block_payload_bytes"], [4096, 4096, 2])

    def test_lost_ack_of_final_4096_byte_block_is_reacked(self):
        port = FakeSerial(fixture_frames(large=True, duplicate_final=True))
        metrics = {}
        with tempfile.TemporaryDirectory() as temp:
            path = receive_capture(port, "listen", Path(temp), timeout=0.1, metrics=metrics)
            self.assertTrue(path.exists())
        acks = [write for write in port.writes if write.startswith(b"DIAG1 ACK")]
        self.assertEqual(acks, [b"DIAG1 ACK 1A2B3C4D 0\n",
                                b"DIAG1 ACK 1A2B3C4D 1\n",
                                b"DIAG1 ACK 1A2B3C4D 2\n",
                                b"DIAG1 ACK 1A2B3C4D 2\n"])
        self.assertEqual(metrics["unique_bytes"], 8194)
        self.assertEqual(metrics["duplicates_observed"], 1)

    def test_late_retry_of_same_block_gets_a_second_matching_ack(self):
        # The duplicate represents a block retransmitted after the first ACK
        # arrived late or was lost; the host must acknowledge it again.
        port = DelayedAckSerial(fixture_frames(duplicate_chunk=True, large=True))
        with tempfile.TemporaryDirectory() as temp:
            path = receive_capture(port, "listen", Path(temp), timeout=0.1)
            self.assertTrue(path.exists())
            self.assertEqual(port.writes[:4], [
                b"DIAG1 ACK 1A2B3C4D 0\n",
                b"DIAG1 ACK 1A2B3C4D 0\n",
                b"DIAG1 ACK 1A2B3C4D 1\n",
                b"DIAG1 ACK 1A2B3C4D 2\n",
            ])
            self.assertTrue(port.writes[4].startswith(b"DIAG1 VERIFIED 1A2B3C4D "))
            self.assertTrue(port.delayed_ack)

    def test_lost_ack_of_final_block_is_reacked_without_duplicate_pixels(self):
        # The sender retransmits the last DATA after its ACK is lost. END follows
        # only after the retransmitted block receives its ACK.
        port = FakeSerial(fixture_frames(duplicate_chunk=True))
        metrics = {}
        with tempfile.TemporaryDirectory() as temp:
            path = receive_capture(port, "listen", Path(temp), timeout=0.1, metrics=metrics)
            self.assertTrue(path.exists())
            acks = [write for write in port.writes if write.startswith(b"DIAG1 ACK")]
            self.assertEqual(acks, [b"DIAG1 ACK 1A2B3C4D 0\n"] * 2)
            self.assertEqual(metrics["unique_bytes"], 4)
            self.assertEqual(metrics["accepted_blocks"], 1)
            self.assertEqual(metrics["duplicates_observed"], 1)

    def test_partial_ack_write_fails_without_verification(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaisesRegex(CaptureError, "Escritura parcial del ACK"):
                receive_capture(PartialAckSerial(fixture_frames()), "listen",
                                Path(temp), timeout=0.1)

    def test_corrupt_block_recovery_is_bounded(self):
        checksum = zlib.crc32(b"\x00\xF8\xE0\x07")
        meta = encode_frame(META, 0x1A2B3C4D, struct.pack("<II", 4, checksum),
                            width=2, height=1, pixel_format=RGB565_LE)
        damaged = bytearray(encode_frame(DATA, 0x1A2B3C4D, b"\x00\xF8\xE0\x07",
                                         width=2, height=1, pixel_format=RGB565_LE, sequence=0))
        damaged[-1] ^= 1
        port = FakeSerial(meta + bytes(damaged) * 4)
        metrics = {}
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaisesRegex(CaptureError, "tras tres reintentos"):
                receive_capture(port, "listen", Path(temp), timeout=0.1, metrics=metrics)
            self.assertEqual(metrics["checksum_rejections"], 4)
            self.assertEqual(metrics["receiver_nak_sent"], 4)
            self.assertEqual(len([w for w in port.writes if w.startswith(b"DIAG1 NAK")]), 4)
            self.assertFalse(any(b"DIAG1 VERIFIED" in write for write in port.writes))

    def test_wrong_capture_id_is_nacked_for_active_capture(self):
        capture_id = 0x1A2B3C4D
        pixels = b"\x00\xF8\xE0\x07"
        checksum = zlib.crc32(pixels)
        meta = encode_frame(META, capture_id, struct.pack("<II", len(pixels), checksum),
                            width=2, height=1, pixel_format=RGB565_LE)
        wrong_id_chunk = encode_frame(DATA, 0x55667788, pixels, width=2, height=1,
                                      pixel_format=RGB565_LE, sequence=0)
        valid_chunk = encode_frame(DATA, capture_id, pixels, width=2, height=1,
                                    pixel_format=RGB565_LE, sequence=0)
        end = encode_frame(END, capture_id, struct.pack("<II", len(pixels), checksum),
                           width=2, height=1, pixel_format=RGB565_LE)
        port = FakeSerial(meta + wrong_id_chunk + valid_chunk + end)
        metrics = {}
        with tempfile.TemporaryDirectory() as temp:
            path = receive_capture(port, "listen", Path(temp), timeout=0.1, metrics=metrics)
            self.assertTrue(path.exists())
        self.assertEqual(port.writes[0], b"DIAG1 NAK 1A2B3C4D 0\n")
        self.assertEqual(metrics["metadata_rejections"], 1)
        self.assertEqual(metrics["accepted_blocks"], 1)

    def test_wrong_sequence_is_nacked_then_expected_sequence_recovers(self):
        capture_id = 0x1A2B3C4D
        pixels = b"\x00\xF8\xE0\x07"
        checksum = zlib.crc32(pixels)
        meta = encode_frame(META, capture_id, struct.pack("<II", len(pixels), checksum),
                            width=2, height=1, pixel_format=RGB565_LE)
        wrong = encode_frame(DATA, capture_id, pixels, width=2, height=1,
                             pixel_format=RGB565_LE, sequence=1)
        correct = encode_frame(DATA, capture_id, pixels, width=2, height=1,
                               pixel_format=RGB565_LE, sequence=0)
        end = encode_frame(END, capture_id, struct.pack("<II", len(pixels), checksum),
                           width=2, height=1, pixel_format=RGB565_LE)
        port = FakeSerial(meta + wrong + correct + end)
        metrics = {}
        with tempfile.TemporaryDirectory() as temp:
            receive_capture(port, "listen", Path(temp), timeout=0.1, metrics=metrics)
        self.assertEqual(port.writes[0], b"DIAG1 NAK 1A2B3C4D 1\n")
        self.assertEqual(metrics["sequence_rejections"], 1)
        self.assertEqual(metrics["accepted_blocks"], 1)

    def test_verified_is_not_sent_when_bmp_cannot_be_saved(self):
        with tempfile.TemporaryDirectory() as temp:
            output_file = Path(temp) / "not-a-directory"
            output_file.write_text("occupied", encoding="utf-8")
            port = FakeSerial(fixture_frames())
            with self.assertRaises(OSError):
                receive_capture(port, "listen", output_file, timeout=0.1)
        self.assertFalse(any(write.startswith(b"DIAG1 VERIFIED") for write in port.writes))

    def test_metrics_names_are_exclusive(self):
        with tempfile.TemporaryDirectory() as temp:
            requested = Path(temp) / "attempt.json"
            first = write_metrics(requested, {"result": "failed"})
            second = write_metrics(requested, {"result": "verified"})
            self.assertNotEqual(first, second)
            self.assertEqual(first.name, "attempt.json")
            self.assertEqual(second.name, "attempt-02.json")
            self.assertEqual(first.read_text(encoding="utf-8").strip(), '{\n  "result": "failed"\n}')
            self.assertEqual(second.read_text(encoding="utf-8").strip(), '{\n  "result": "verified"\n}')

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
