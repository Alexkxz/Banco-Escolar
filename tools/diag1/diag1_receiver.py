#!/usr/bin/env python3
"""Windows host receiver for the Banco Escolar DIAG.1 serial capture protocol."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import struct
import time
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO

MAGIC = b"BED1"
VERSION = 1
HEADER_SIZE = 32
MAX_FRAME_PAYLOAD = 4096
MAX_IMAGE_BYTES = 2 * 1024 * 1024
MAX_REQUEST_ATTEMPTS = 5
MAX_TRANSFER_SECONDS = 900.0
RGB565_LE = 1
REQUEST = b"DIAG1 CAPTURE\n"
SUPPORTED_BAUD_RATES = (115200, 460800)
FIRMWARE_METRICS_DRAIN_SECONDS = 60.0

META, DATA, END, STATUS, ERROR = 1, 2, 3, 4, 5
PREFIX = struct.Struct("<4sBBHIHHBBHII")
HEADER = struct.Struct("<4sBBHIHHBBHIII")


class CaptureError(Exception):
    pass


class TransferTimeout(CaptureError):
    pass


@dataclass(frozen=True)
class Frame:
    kind: int
    capture_id: int
    width: int
    height: int
    pixel_format: int
    sequence: int
    payload: bytes
    payload_ok: bool


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def encode_frame(kind: int, capture_id: int, payload: bytes = b"", *,
                 width: int = 0, height: int = 0, pixel_format: int = 0,
                 sequence: int = 0) -> bytes:
    prefix = PREFIX.pack(MAGIC, VERSION, kind, HEADER_SIZE, capture_id,
                         width, height, pixel_format, 0, sequence,
                         len(payload), crc32(payload))
    return prefix + struct.pack("<I", crc32(prefix)) + payload


def read_exact(port, count: int, deadline: float) -> bytes:
    result = bytearray()
    while len(result) < count:
        if time.monotonic() >= deadline:
            raise TransferTimeout("Timeout: transferencia incompleta")
        block = port.read(count - len(result))
        if block:
            result.extend(block)
        else:
            time.sleep(0.001)
    return bytes(result)


def read_frame(port, timeout: float = 15.0) -> Frame:
    """Find a versioned frame in a stream that can also contain monitor text."""
    deadline = time.monotonic() + timeout
    window = bytearray()
    while True:
        byte = read_exact(port, 1, deadline)
        window.extend(byte)
        if len(window) > len(MAGIC):
            del window[0]
        if bytes(window) == MAGIC:
            break

    raw_header = MAGIC + read_exact(port, HEADER_SIZE - len(MAGIC), deadline)
    fields = HEADER.unpack(raw_header)
    (magic, version, kind, header_size, capture_id, width, height,
     pixel_format, reserved, sequence, payload_length, payload_crc,
     header_crc) = fields
    if (magic != MAGIC or version != VERSION or header_size != HEADER_SIZE or
            reserved != 0 or crc32(raw_header[:28]) != header_crc):
        raise CaptureError("Cabecera DIAG.1 inválida o checksum de cabecera incorrecto")
    if kind not in (META, DATA, END, STATUS, ERROR):
        raise CaptureError(f"Tipo de trama DIAG.1 desconocido: {kind}")
    if payload_length > MAX_FRAME_PAYLOAD:
        raise CaptureError("Longitud de trama superior al máximo del protocolo")
    payload = read_exact(port, payload_length, deadline)
    return Frame(kind, capture_id, width, height, pixel_format, sequence,
                 payload, crc32(payload) == payload_crc)


class ImageAssembler:
    """Validates ordered chunks and accepts a byte-identical retransmission."""

    def __init__(self, capture_id: int, total_length: int, checksum: int,
                 width: int, height: int, pixel_format: int):
        if width <= 0 or height <= 0 or width * height * 2 != total_length:
            raise CaptureError("Dimensiones o longitud de imagen inconsistentes")
        if total_length > MAX_IMAGE_BYTES:
            raise CaptureError("La imagen excede el límite de memoria del receptor")
        if pixel_format != RGB565_LE:
            raise CaptureError(f"Formato de píxel no soportado: {pixel_format}")
        self.capture_id = capture_id
        self.total_length = total_length
        self.checksum = checksum
        self.width = width
        self.height = height
        self.pixel_format = pixel_format
        self.data = bytearray()
        self.next_sequence = 0
        self.last_chunk: bytes | None = None

    def add_chunk(self, frame: Frame) -> str:
        if (frame.capture_id != self.capture_id or frame.width != self.width or
                frame.height != self.height or frame.pixel_format != self.pixel_format):
            return "retry"
        if not frame.payload_ok:
            return "retry"
        if frame.sequence == self.next_sequence - 1 and frame.payload == self.last_chunk:
            return "duplicate"
        if frame.sequence != self.next_sequence:
            return "retry"
        expected = min(MAX_FRAME_PAYLOAD, self.total_length - len(self.data))
        if expected <= 0 or len(frame.payload) != expected:
            return "retry"
        self.data.extend(frame.payload)
        self.last_chunk = frame.payload
        self.next_sequence += 1
        return "accepted"

    def verify_end(self, frame: Frame) -> None:
        if (frame.kind != END or frame.capture_id != self.capture_id or
                frame.width != self.width or frame.height != self.height or
                frame.pixel_format != self.pixel_format or
                not frame.payload_ok or len(frame.payload) != 8):
            raise CaptureError("Fin de transferencia ausente o inválido")
        length, checksum = struct.unpack("<II", frame.payload)
        actual = crc32(self.data)
        if (length != self.total_length or len(self.data) != self.total_length or
                checksum != self.checksum or actual != self.checksum):
            raise CaptureError("La imagen quedó incompleta o su checksum no coincide")


def wait_for_meta(port, mode: str, timeout: float = 25.0,
                  request_interval: float = 1.0) -> tuple[Frame, int, int]:
    deadline = time.monotonic() + timeout
    next_request = 0.0
    requests_sent = 0
    while time.monotonic() < deadline:
        now = time.monotonic()
        if (mode == "request" and now >= next_request and
                requests_sent < MAX_REQUEST_ATTEMPTS):
            port.write(REQUEST)
            port.flush()
            requests_sent += 1
            next_request = now + request_interval
        try:
            frame = read_frame(port, min(0.5, max(0.01, deadline - now)))
        except TransferTimeout:
            continue
        if frame.kind == ERROR:
            message = frame.payload.decode("ascii", errors="replace")
            if message.startswith("BUSY:"):
                print("La terminal ya tiene una captura en curso; esperando su respuesta.")
                continue
            raise CaptureError(message or "La terminal rechazó la captura")
        if frame.kind == META:
            return frame, requests_sent, deadline
        if frame.kind == STATUS:
            print(frame.payload.decode("utf-8", errors="replace"))
    raise TransferTimeout("Timeout esperando el inicio de captura DIAG.1")


def send_ack(port, capture_id: int, sequence: int, accepted: bool) -> None:
    command = "ACK" if accepted else "NAK"
    message = f"DIAG1 {command} {capture_id:08X} {sequence}\n".encode("ascii")
    if port.write(message) != len(message):
        raise CaptureError(f"Escritura parcial del {command} para secuencia {sequence}")
    port.flush()


def send_capture_verified(port, capture_id: int, checksum: int) -> None:
    message = f"DIAG1 VERIFIED {capture_id:08X} {checksum:08X}\n".encode("ascii")
    if port.write(message) != len(message):
        raise CaptureError("No se pudo confirmar la imagen verificada a la terminal")
    port.flush()


def unique_capture_path(directory: Path, capture_id: int) -> Path:
    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    stem = f"terminal-{stamp}-{capture_id:08X}"
    candidate = directory / f"{stem}.bmp"
    suffix = 2
    while candidate.exists():
        candidate = directory / f"{stem}-{suffix:02d}.bmp"
        suffix += 1
    return candidate


def write_bmp(path: Path, assembler: ImageAssembler) -> None:
    width, height = assembler.width, assembler.height
    row_bytes = width * 3
    padded_row_bytes = (row_bytes + 3) & ~3
    pixels_size = padded_row_bytes * height
    file_size = 54 + pixels_size
    header = struct.pack("<2sIHHI", b"BM", file_size, 0, 0, 54)
    dib = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0,
                      pixels_size, 2835, 2835, 0, 0)
    temporary = path.with_suffix(path.suffix + ".part")
    try:
        with temporary.open("xb") as output:
            output.write(header)
            output.write(dib)
            for y in range(height - 1, -1, -1):
                source_row = y * width * 2
                row = bytearray()
                for x in range(width):
                    pixel = assembler.data[source_row + x * 2] | (assembler.data[source_row + x * 2 + 1] << 8)
                    red = (pixel >> 11) & 0x1F
                    green = (pixel >> 5) & 0x3F
                    blue = pixel & 0x1F
                    row.extend(((blue << 3) | (blue >> 2),
                                (green << 2) | (green >> 4),
                                (red << 3) | (red >> 2)))
                row.extend(b"\x00" * (padded_row_bytes - len(row)))
                output.write(row)
            output.flush()
            os.fsync(output.fileno())
        # Publish only a complete image. The unique destination is never
        # overwritten, and a failed write leaves no file with a .bmp suffix.
        temporary.rename(path)
    except Exception:
        try:
            temporary.unlink(missing_ok=True)
        except OSError:
            pass
        raise


def receive_capture(port, mode: str, output_dir: Path, *, timeout: float = 25.0,
                    metrics: dict | None = None, baud: int = 115200) -> Path:
    metrics = metrics if metrics is not None else {}
    if baud not in SUPPORTED_BAUD_RATES:
        raise ValueError(f"Velocidad no soportada: {baud}")
    started = time.monotonic()
    metrics.update({"mode": mode, "receiver_configured_baud": baud,
                    "firmware_baud_confirmed": False, "result": "failed",
                    "error": None, "capture_id": None, "width": None, "height": None,
                    "image_bytes": 0, "unique_bytes": 0, "accepted_blocks": 0,
                    "receiver_ack_sent": 0, "receiver_nak_sent": 0,
                    "firmware_ack_received_count": None,
                    "duplicates_observed": 0,
                    "checksum_rejections": 0, "sequence_rejections": 0,
                    "metadata_rejections": 0,
                    "errors": [], "data_frame_receive_times": [],
                    "accepted_block_payload_bytes": []})
    meta_started = time.monotonic()
    transfer_started = None
    try:
        meta, requests_sent, _deadline = wait_for_meta(port, mode, timeout)
        metrics["initial_capture_wait_seconds"] = time.monotonic() - meta_started
        metrics["capture_id"] = f"{meta.capture_id:08X}"
        metrics["width"], metrics["height"] = meta.width, meta.height
        if not meta.payload_ok or len(meta.payload) != 8:
            raise CaptureError("Metadatos de captura inválidos")
        total_length, checksum = struct.unpack("<II", meta.payload)
        metrics["image_bytes"] = total_length
        assembler = ImageAssembler(meta.capture_id, total_length, checksum,
                                   meta.width, meta.height, meta.pixel_format)
        transfer_started = time.monotonic()
        transfer_deadline = transfer_started + MAX_TRANSFER_SECONDS
        retries: dict[int, int] = {}
        while True:
            remaining = transfer_deadline - time.monotonic()
            if remaining <= 0:
                raise TransferTimeout("Timeout durante la transferencia o esperando END")
            block_started = time.monotonic()
            frame = read_frame(port, min(15.0, remaining))
            block_elapsed = time.monotonic() - block_started
            if frame.kind in (STATUS, ERROR):
                message = frame.payload.decode("utf-8", errors="replace")
                if frame.kind == ERROR and not message.startswith("BUSY:"):
                    raise CaptureError(message or "Error durante la captura")
                if message:
                    print(message)
                continue
            if frame.kind == END:
                assembler.verify_end(frame)
                metrics["transfer_seconds"] = time.monotonic() - transfer_started
                break
            if frame.kind != DATA:
                continue
            metrics["data_frame_receive_times"].append({
                "sequence": frame.sequence, "receive_ms": round(block_elapsed * 1000, 3)})
            if frame.capture_id != assembler.capture_id:
                metrics["errors"].append("capture_id_mismatch")
            result = assembler.add_chunk(frame)
            if result == "accepted":
                metrics["accepted_blocks"] += 1
                metrics["unique_bytes"] = len(assembler.data)
                metrics["accepted_block_payload_bytes"].append(len(frame.payload))
                send_ack(port, assembler.capture_id, frame.sequence, True)
                metrics["receiver_ack_sent"] += 1
            elif result == "duplicate":
                metrics["duplicates_observed"] += 1
                send_ack(port, assembler.capture_id, frame.sequence, True)
                metrics["receiver_ack_sent"] += 1
            else:
                retries[frame.sequence] = retries.get(frame.sequence, 0) + 1
                if not frame.payload_ok:
                    metrics["checksum_rejections"] += 1
                elif (frame.capture_id != assembler.capture_id or
                      frame.width != assembler.width or frame.height != assembler.height or
                      frame.pixel_format != assembler.pixel_format):
                    metrics["metadata_rejections"] += 1
                else:
                    metrics["sequence_rejections"] += 1
                send_ack(port, assembler.capture_id, frame.sequence, False)
                metrics["receiver_nak_sent"] += 1
                if retries[frame.sequence] > 3:
                    raise CaptureError("Bloque rechazado tras tres reintentos")

        conversion_started = time.monotonic()
        output_dir.mkdir(parents=True, exist_ok=True)
        path = unique_capture_path(output_dir, assembler.capture_id)
        write_bmp(path, assembler)
        metrics["bmp_conversion_and_save_seconds"] = time.monotonic() - conversion_started
        metrics["bmp_bytes"] = path.stat().st_size
        send_capture_verified(port, assembler.capture_id, assembler.checksum)
        try:
            status = read_frame(port, 1.5)
            if status.kind == STATUS:
                print(status.payload.decode("utf-8", errors="replace"))
        except TransferTimeout:
            pass
        metrics["result"] = "verified"
        metrics["output"] = str(path)
        metrics["requests_sent"] = requests_sent
        return path
    except Exception as error:
        metrics["error"] = f"{type(error).__name__}: {error}"
        metrics["errors"].append(metrics["error"])
        raise
    finally:
        metrics.setdefault("initial_capture_wait_seconds", time.monotonic() - meta_started)
        metrics["total_seconds"] = time.monotonic() - started
        if transfer_started is not None and "transfer_seconds" not in metrics:
            metrics["transfer_elapsed_until_failure_seconds"] = time.monotonic() - transfer_started
        if metrics.get("transfer_seconds") and metrics.get("unique_bytes"):
            metrics["effective_bytes_per_second"] = round(
                metrics["unique_bytes"] / metrics["transfer_seconds"], 3)
            metrics["effective_mib_per_second"] = round(
                metrics["unique_bytes"] / metrics["transfer_seconds"] / (1024 * 1024), 6)
def choose_port(serial_module) -> str:
    ports = list(serial_module.tools.list_ports.comports())
    if not ports:
        raise CaptureError("No se encontraron puertos seriales. Conecta la terminal por USB.")
    print("Puertos disponibles:")
    for index, port in enumerate(ports, 1):
        print(f"  {index}. {port.device} — {port.description}")
    while True:
        value = input("Selecciona el número del puerto: ").strip()
        if value.isdigit() and 1 <= int(value) <= len(ports):
            return ports[int(value) - 1].device
        print("Selección no válida.")


def write_metrics(path: Path, metrics: dict) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    suffix = 1
    while True:
        candidate = path if suffix == 1 else path.with_name(f"{path.stem}-{suffix:02d}{path.suffix}")
        try:
            with candidate.open("x", encoding="utf-8", newline="\n") as output:
                json.dump(metrics, output, ensure_ascii=False, indent=2)
                output.write("\n")
            return candidate
        except FileExistsError:
            suffix = 2 if suffix == 1 else suffix + 1


def collect_firmware_diagnostics(port, metrics: dict, *, timeout: float = FIRMWARE_METRICS_DRAIN_SECONDS) -> None:
    """Collect the firmware's post-transfer summary while this process still owns USB serial."""
    if not hasattr(port, "readline") or metrics.get("capture_id") is None:
        return
    try:
        expected_blocks = (int(metrics.get("image_bytes") or 0) + MAX_FRAME_PAYLOAD - 1) // MAX_FRAME_PAYLOAD
        lines: list[str] = []
        summary: dict[str, str] = {}
        block_timings: list[dict[str, int]] = []
        attempt_timings: list[dict[str, int]] = []
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            raw = port.readline()
            if not raw:
                continue
            line = raw.decode("ascii", errors="replace").strip()
            if line.startswith("DIAG1_FW_METRICS "):
                lines.append(line)
                summary = dict(token.split("=", 1) for token in line.split()[1:] if "=" in token)
            elif line.startswith("DIAG1_FW_BLOCK "):
                lines.append(line)
                fields = dict(token.split("=", 1) for token in line.split()[1:] if "=" in token)
                try:
                    block_timings.append({key: int(fields[key]) for key in ("seq", "send_ms", "ack_wait_ms")})
                except (KeyError, ValueError):
                    continue
            elif line.startswith("DIAG1_FW_ATTEMPT "):
                lines.append(line)
                fields = dict(token.split("=", 1) for token in line.split()[1:] if "=" in token)
                keys = ("seq", "retry", "polls", "no_space", "space_samples", "space_avg", "space_max",
                        "write_calls", "bytes_accepted", "write_active_us", "poll_gap_avg_us",
                        "poll_gap_max_us", "ack_latency_us")
                try:
                    attempt_timings.append({key: int(fields[key]) for key in keys})
                except (KeyError, ValueError):
                    continue
            expected_attempts = int(summary.get("blocks_sent_attempts", "0") or 0)
            if summary and (len(attempt_timings) >= expected_attempts if expected_attempts else
                            len(block_timings) >= expected_blocks):
                break
        metrics["firmware_metrics"] = {
            "received": bool(summary),
            "summary": summary or None,
            "block_timings": block_timings,
            "attempt_timings": attempt_timings,
            "raw_lines": lines,
        }
    except Exception as error:
        # Instrumentation collection must never replace the transfer result/error.
        metrics["firmware_metrics_collection_error"] = f"{type(error).__name__}: {error}"


def create_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Recibe una captura DIAG.1 por USB y la guarda como BMP.")
    parser.add_argument("--port", help="Puerto COM, por ejemplo COM5; omítelo para elegirlo en una lista")
    parser.add_argument("--mode", choices=("request", "listen"), default="request",
                        help="request solicita una captura; listen espera el botón del Menú Maestro")
    parser.add_argument("--output", type=Path, default=Path("capturas_terminal"),
                        help="Carpeta de salida; se crean nombres exclusivos")
    parser.add_argument("--timeout", type=float, default=25.0, help="Timeout para iniciar una captura")
    parser.add_argument("--baud", type=int, choices=SUPPORTED_BAUD_RATES, default=115200,
                        help="Velocidad configurada en el receptor; no verifica la del firmware")
    parser.add_argument("--metrics", type=Path, metavar="JSON_PATH",
                        help="Ruta JSON de métricas; se conserva como archivo distinto, incluso si falla")
    return parser


def main() -> int:
    args = create_argument_parser().parse_args()

    try:
        import serial
        from serial.tools import list_ports  # noqa: F401
    except ImportError:
        print("Falta pyserial. Instálalo con: py -m pip install -r tools/diag1/requirements.txt")
        return 2

    metrics = {} if args.metrics is not None else None
    connection = None
    exit_code = 0
    cli_started = time.monotonic()
    try:
        port_name = args.port or choose_port(serial)
        connection = serial.Serial(port=None, baudrate=args.baud, timeout=0.2, write_timeout=2)
        connection.port = port_name
        connection.dtr = False
        connection.rts = False
        connection.open()
        print(f"DIAG.1 en {port_name} a {args.baud} baudios (configuración del receptor); modo {args.mode}.")
        print("En modo listen, pulsa Capturar por USB en el Menú Maestro.")
        path = receive_capture(connection, args.mode, args.output, timeout=args.timeout,
                               metrics=metrics, baud=args.baud)
        print(f"Captura verificada guardada en: {path.resolve()}")
    except (CaptureError, OSError) as error:
        print(f"DIAG.1: {error}")
        exit_code = 1
        if metrics is not None and not metrics:
            metrics.update({"mode": args.mode, "receiver_configured_baud": args.baud,
                            "firmware_baud_confirmed": False, "result": "failed",
                            "error": f"{type(error).__name__}: {error}",
                            "errors": [f"{type(error).__name__}: {error}"],
                            "initial_capture_wait_seconds": None,
                            "total_seconds": time.monotonic() - cli_started})
    finally:
        if metrics is not None:
            collect_firmware_diagnostics(connection, metrics)
        if connection is not None and connection.is_open:
            try:
                connection.close()
            except OSError as close_error:
                print(f"No se pudo confirmar el cierre del puerto: {close_error}")
        if metrics is not None:
            try:
                metrics_path = write_metrics(args.metrics, metrics)
                print(f"Métricas guardadas en: {metrics_path.resolve()}")
            except OSError as metrics_error:
                print(f"No se pudieron guardar las métricas DIAG.1: {metrics_error}")
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
