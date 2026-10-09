#!/usr/bin/env python3
"""Windows host receiver for the Banco Escolar DIAG.1 serial capture protocol."""

from __future__ import annotations

import argparse
import datetime as dt
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
MAX_FRAME_PAYLOAD = 1024
MAX_IMAGE_BYTES = 2 * 1024 * 1024
MAX_REQUEST_ATTEMPTS = 5
MAX_TRANSFER_SECONDS = 900.0
RGB565_LE = 1
REQUEST = b"DIAG1 CAPTURE\n"

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
            raise CaptureError("El bloque pertenece a otra captura")
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
    port.write(f"DIAG1 {command} {capture_id:08X} {sequence}\n".encode("ascii"))
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


def receive_capture(port, mode: str, output_dir: Path, *, timeout: float = 25.0) -> Path:
    meta, _requests_sent, _deadline = wait_for_meta(port, mode, timeout)
    if not meta.payload_ok or len(meta.payload) != 8:
        raise CaptureError("Metadatos de captura inválidos")
    total_length, checksum = struct.unpack("<II", meta.payload)
    assembler = ImageAssembler(meta.capture_id, total_length, checksum,
                               meta.width, meta.height, meta.pixel_format)
    transfer_deadline = time.monotonic() + MAX_TRANSFER_SECONDS
    retries: dict[int, int] = {}
    while len(assembler.data) < assembler.total_length:
        remaining = transfer_deadline - time.monotonic()
        if remaining <= 0:
            raise TransferTimeout("Timeout durante la transferencia de imagen")
        frame = read_frame(port, min(15.0, remaining))
        if frame.kind in (STATUS, ERROR):
            message = frame.payload.decode("utf-8", errors="replace")
            if frame.kind == ERROR and not message.startswith("BUSY:"):
                raise CaptureError(message or "Error durante la captura")
            if message:
                print(message)
            continue
        if frame.kind != DATA or frame.capture_id != assembler.capture_id:
            continue
        result = assembler.add_chunk(frame)
        if result == "accepted":
            send_ack(port, frame.capture_id, frame.sequence, True)
        elif result == "duplicate":
            send_ack(port, frame.capture_id, frame.sequence, True)
        else:
            retries[frame.sequence] = retries.get(frame.sequence, 0) + 1
            send_ack(port, frame.capture_id, frame.sequence, False)
            if retries[frame.sequence] > 3:
                raise CaptureError("Checksum incorrecto tras tres reintentos")

    while True:
        remaining = transfer_deadline - time.monotonic()
        if remaining <= 0:
            raise TransferTimeout("Timeout esperando la confirmación final")
        frame = read_frame(port, min(15.0, remaining))
        if frame.kind in (STATUS, ERROR):
            message = frame.payload.decode("utf-8", errors="replace")
            if frame.kind == STATUS:
                print(message)
                continue
            if message.startswith("BUSY:"):
                print(message)
                continue
            raise CaptureError(message or "Error al finalizar la captura")
        if frame.kind == END:
            assembler.verify_end(frame)
            break

    output_dir.mkdir(parents=True, exist_ok=True)
    path = unique_capture_path(output_dir, assembler.capture_id)
    write_bmp(path, assembler)
    send_capture_verified(port, assembler.capture_id, assembler.checksum)
    try:
        status = read_frame(port, 1.5)
        if status.kind == STATUS:
            print(status.payload.decode("utf-8", errors="replace"))
    except TransferTimeout:
        pass
    return path


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


def main() -> int:
    parser = argparse.ArgumentParser(description="Recibe una captura DIAG.1 por USB y la guarda como BMP.")
    parser.add_argument("--port", help="Puerto COM, por ejemplo COM5; omítelo para elegirlo en una lista")
    parser.add_argument("--mode", choices=("request", "listen"), default="request",
                        help="request solicita una captura; listen espera el botón del Menú Maestro")
    parser.add_argument("--output", type=Path, default=Path("capturas_terminal"),
                        help="Carpeta de salida; se crean nombres exclusivos")
    parser.add_argument("--timeout", type=float, default=25.0, help="Timeout para iniciar una captura")
    args = parser.parse_args()

    try:
        import serial
        from serial.tools import list_ports  # noqa: F401
    except ImportError:
        print("Falta pyserial. Instálalo con: py -m pip install -r tools/diag1/requirements.txt")
        return 2

    try:
        port_name = args.port or choose_port(serial)
        connection = serial.Serial(port=None, baudrate=115200, timeout=0.2, write_timeout=2)
        connection.port = port_name
        connection.dtr = False
        connection.rts = False
        connection.open()
        try:
            print(f"DIAG.1 en {port_name} a 115200 baudios; modo {args.mode}.")
            print("En modo listen, pulsa Capturar por USB en el Menú Maestro.")
            path = receive_capture(connection, args.mode, args.output, timeout=args.timeout)
            print(f"Captura verificada guardada en: {path.resolve()}")
            return 0
        finally:
            connection.close()
    except (CaptureError, OSError) as error:
        print(f"DIAG.1: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
