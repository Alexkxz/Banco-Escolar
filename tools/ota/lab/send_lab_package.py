#!/usr/bin/env python3
"""Send one prebuilt OTA-ARQ.2 TEST-ONLY package to the lab installer."""
import argparse
import hashlib
import json
import sys
from pathlib import Path

EXPECTED = {
    "model": "WAVESHARE_ESP32_S3_TOUCH_LCD_7_REV1_2",
    "version": "1.2.4-lab",
    "target": "ota_1",
    "offset": 0x650000,
    "slot_size": 0x640000,
    "build_id": "OTA-ARQ.2-CANDIDATE",
    "data_schema": 1,
    "rollback_compatible": True,
}
OPERATOR_COMMAND = b"INSTALL OTA1 OTA-ARQ.2\n"
PHYSICAL_CONFIRMATION = "I AUTHORIZE SENDING OTA-ARQ.2 TO ota_1"


def decode_package(data: bytes):
    if len(data) < 3:
        raise ValueError("paquete truncado")
    manifest_length = int.from_bytes(data[:2], "little")
    if manifest_length == 0 or manifest_length > 2048 or 2 + manifest_length >= len(data):
        raise ValueError("longitud de manifiesto inválida")
    try:
        manifest = json.loads(data[2:2 + manifest_length].decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError("manifiesto JSON inválido") from error
    image = data[2 + manifest_length:]
    expected_fields = set(EXPECTED) | {"size", "sha256", "signature"}
    if not isinstance(manifest, dict) or set(manifest) != expected_fields:
        raise ValueError("campos de manifiesto distintos a OTA-ARQ.2")
    if any(manifest.get(key) != value for key, value in EXPECTED.items()):
        raise ValueError("identidad, destino o compatibilidad rechazada")
    if manifest.get("size") != len(image) or len(image) == 0 or len(image) > EXPECTED["slot_size"]:
        raise ValueError("longitud de imagen no coincide o excede ota_1")
    digest = hashlib.sha256(image).hexdigest()
    if manifest.get("sha256") != digest:
        raise ValueError("SHA-256 local no coincide con el manifiesto")
    if not isinstance(manifest.get("signature"), str) or not manifest["signature"]:
        raise ValueError("firma ausente")
    return manifest, image


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", required=True, type=Path)
    parser.add_argument("--port", required=True, help="Puerto explícito; no se autodetecta")
    parser.add_argument("--baud", type=int, default=460800)
    parser.add_argument("--confirm-physical", required=True,
                        help=f"debe ser exactamente: {PHYSICAL_CONFIRMATION}")
    args = parser.parse_args()
    if args.confirm_physical != PHYSICAL_CONFIRMATION:
        raise SystemExit("RECHAZADO: falta la frase explícita de autorización física")

    try:
        data = args.package.resolve(strict=True).read_bytes()
        manifest, image = decode_package(data)
    except (OSError, ValueError) as error:
        raise SystemExit(f"RECHAZADO: {error}") from error
    print(f"Candidato: {manifest['build_id']} {manifest['version']}")
    print(f"Destino: {manifest['target']} @ 0x{manifest['offset']:06X}; {len(image)} bytes")
    print(f"SHA-256: {manifest['sha256']}")
    print(f"Paquete SHA-256: {hashlib.sha256(data).hexdigest()}")

    try:
        import serial  # type: ignore
    except ImportError as error:
        raise SystemExit("RECHAZADO: falta pyserial en el Python utilizado") from error

    # Set inactive modem controls before opening. This tool never requests a
    # bootloader reset; the operator must have separately approved the action.
    port = serial.Serial(port=None, baudrate=args.baud, timeout=1, write_timeout=120,
                         dsrdtr=False, rtscts=False)
    port.port = args.port
    port.dtr = False
    port.rts = False
    try:
        port.open()
        port.write(OPERATOR_COMMAND + data)
        port.flush()
        while True:
            line = port.readline()
            if not line:
                continue
            decoded = line.decode("utf-8", errors="replace").rstrip()
            print(decoded)
            if "ota_1 verificada y seleccionada" in decoded:
                return 0
            if "[OTA-LAB-INSTALLER] RECHAZADO:" in decoded:
                return 1
    except (OSError, serial.SerialException) as error:
        print(f"Transporte interrumpido: {error}; el dispositivo debe dejar ota_0 seleccionado si aún no llamó al selector.",
              file=sys.stderr)
        return 1
    finally:
        if port.is_open:
            port.close()


if __name__ == "__main__":
    raise SystemExit(main())
