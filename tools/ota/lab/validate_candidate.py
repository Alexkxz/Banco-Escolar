#!/usr/bin/env python3
"""Read-only verification of the OTA-LAB candidate artifact and manifest."""

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--esptool-script", required=True, type=Path,
                        help="Path to the installed esptool.py used for image_info")
    parser.add_argument("--partition-table-bin", required=True, type=Path)
    args = parser.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8-sig"))
    image = args.image.resolve(strict=True)
    partition_table = args.partition_table_bin.resolve(strict=True)
    content = image.read_bytes()
    digest = hashlib.sha256(content).hexdigest()
    partition_digest = hashlib.sha256(partition_table.read_bytes()).hexdigest()
    if len(content) != manifest["firmware_size_bytes"]:
        raise SystemExit("RECHAZADO: tamaño distinto al manifiesto")
    if digest != manifest["firmware_sha256"].lower():
        raise SystemExit("RECHAZADO: SHA-256 distinto al manifiesto")
    if (manifest.get("build_id") != "OTA-LAB.1" or
            manifest.get("ota_wifi_enabled") is not False or
            manifest.get("rollback_enabled") is not True):
        raise SystemExit("RECHAZADO: identidad o estado OTA no corresponde a OTA-LAB.1")
    if partition_digest != manifest.get("partition_table_bin_sha256", "").lower():
        raise SystemExit("RECHAZADO: tabla de particiones distinta al manifiesto")
    if manifest.get("target") != {"name": "ota_1", "offset": "0x650000", "size": "0x640000"}:
        raise SystemExit("RECHAZADO: destino fuera de ota_1")
    if manifest.get("ota0_reference") != {
        "size_bytes": 2058080,
        "sha256": "5bc7f52f0d4a9448fbbfd89ecbda24dad37fb3872882c6dce68ce84429c27709",
    }:
        raise SystemExit("RECHAZADO: referencia ota_0 no coincide con la línea base aprobada")

    result = subprocess.run(
        [sys.executable, str(args.esptool_script.resolve(strict=True)), "image_info", str(image)],
        check=False, capture_output=True, text=True,
    )
    output = result.stdout + result.stderr
    valid_checksum = re.search(r"Checksum:\s+[^\r\n]+\(valid\)", output) is not None
    valid_hash = ("Validation Hash:" not in output or
                  re.search(r"Validation Hash:\s+[^\r\n]+\(valid\)", output) is not None)
    if (result.returncode != 0 or "Detected image type: ESP32-S3" not in output or
            not valid_checksum or not valid_hash):
        raise SystemExit("RECHAZADO: esptool no validó la imagen ESP32-S3\n" + output)
    print(f"APROBADO localmente: {image}")
    print(f"Tamaño: {len(content)} bytes")
    print(f"SHA-256: {digest}")
    print(f"Particiones SHA-256: {partition_digest}")
    print("esptool image_info: ESP32-S3 / checksum reportado")
    print("Sin conexión serial ni acceso a flash.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
