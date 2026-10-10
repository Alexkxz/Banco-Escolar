#!/usr/bin/env python3
"""Send read-only HTTP probes to a Banco Escolar terminal on the school LAN."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import http.client
import ipaddress
from pathlib import Path
import sys
from typing import Callable
import uuid


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SAMPLE_ROOT = ROOT / "tools/web/sample_sd/panel-test"
MAX_RESPONSE_BYTES = 2 * 1024 * 1024
PAGE_MARKER = "La página llegó desde la microSD"
DOWNLOAD_PATH = "/panel-test/terminal.svg"


@dataclass(frozen=True)
class HttpResult:
    status: int
    headers: dict[str, str]
    body: bytes


HttpRequest = Callable[[str, int, str, str, float], HttpResult]


def http_request(host: str, port: int, method: str, path: str, timeout: float) -> HttpResult:
    connection = http.client.HTTPConnection(host, port, timeout=timeout)
    try:
        connection.request(method, path)
        response = connection.getresponse()
        body = response.read(MAX_RESPONSE_BYTES + 1)
        if len(body) > MAX_RESPONSE_BYTES:
            raise RuntimeError(f"respuesta demasiado grande en {method} {path}")
        return HttpResult(
            status=response.status,
            headers={key.lower(): value for key, value in response.getheaders()},
            body=body,
        )
    finally:
        connection.close()


def run_checks(
    terminal_ip: str,
    port: int = 80,
    sample_root: Path = DEFAULT_SAMPLE_ROOT,
    timeout: float = 5.0,
    request: HttpRequest = http_request,
    report: Callable[[str], None] = print,
) -> None:
    address = ipaddress.ip_address(terminal_ip)
    host = address.compressed
    sample_root = sample_root.resolve()
    index_path = sample_root / "index.html"
    version_path = sample_root / "version.txt"
    image_path = sample_root / "terminal.svg"
    for required in (index_path, version_path, image_path):
        if not required.is_file():
            raise RuntimeError(f"falta el archivo local de referencia: {required}")

    root = request(host, port, "GET", "/", timeout)
    assert root.status == 200, f"GET /: se esperaba 200 y llegó {root.status}"
    assert root.headers.get("content-type", "").startswith("text/html"), "GET / no devolvió HTML"
    assert PAGE_MARKER in root.body.decode("utf-8"), "GET / no contiene la página de muestra esperada"
    report("PASS GET / — página de muestra recibida")

    status = request(host, port, "GET", "/status", timeout)
    assert status.status == 200, f"GET /status: se esperaba 200 y llegó {status.status}"
    try:
        status_json = __import__("json").loads(status.body.decode("utf-8"))
    except (UnicodeDecodeError, ValueError) as error:
        raise AssertionError("GET /status no devolvió JSON válido") from error
    expected_version = version_path.read_text(encoding="ascii").strip()
    assert status_json.get("sd_mounted") is True, "GET /status indica que la microSD no está montada"
    assert status_json.get("page_version") == expected_version, (
        "GET /status no coincide con la versión local de referencia "
        f"({expected_version!r})"
    )
    report(f"PASS GET /status — microSD montada, página {expected_version}")

    downloaded = request(host, port, "GET", DOWNLOAD_PATH, timeout)
    expected_image = image_path.read_bytes()
    assert downloaded.status == 200, (
        f"GET {DOWNLOAD_PATH}: se esperaba 200 y llegó {downloaded.status}"
    )
    assert downloaded.headers.get("content-type", "").startswith("image/svg+xml"), (
        f"GET {DOWNLOAD_PATH} no devolvió SVG"
    )
    assert downloaded.body == expected_image, f"GET {DOWNLOAD_PATH}: el contenido no coincide"
    report(f"PASS GET {DOWNLOAD_PATH} — descarga íntegra ({len(expected_image)} bytes)")

    missing_path = f"/panel-test/__http_probe_missing_{uuid.uuid4().hex}.txt"
    missing = request(host, port, "GET", missing_path, timeout)
    assert missing.status == 404, f"GET {missing_path}: se esperaba 404 y llegó {missing.status}"
    report("PASS archivo inexistente — HTTP 404")

    invalid = request(host, port, "GET", "/not-panel-test", timeout)
    assert invalid.status == 400, f"GET /not-panel-test: se esperaba 400 y llegó {invalid.status}"
    report("PASS ruta inválida — HTTP 400")

    method = request(host, port, "POST", "/", timeout)
    assert method.status == 405, f"POST /: se esperaba 405 y llegó {method.status}"
    report("PASS método no admitido — HTTP 405")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Verifica por HTTP el servidor de archivos de prueba de Banco Escolar."
    )
    parser.add_argument("terminal_ip", help="IP actual de la terminal en el router Wi-Fi")
    parser.add_argument("--port", type=int, default=80, help="puerto HTTP (predeterminado: 80)")
    parser.add_argument("--timeout", type=float, default=5.0, help="timeout por solicitud, en segundos")
    parser.add_argument(
        "--sample-root",
        type=Path,
        default=DEFAULT_SAMPLE_ROOT,
        help="carpeta local panel-test usada como referencia de contenido",
    )
    args = parser.parse_args(argv)
    try:
        ipaddress.ip_address(args.terminal_ip)
    except ValueError as error:
        parser.error(f"IP inválida: {error}")
    if not 1 <= args.port <= 65535:
        parser.error("--port debe estar entre 1 y 65535")
    if args.timeout <= 0:
        parser.error("--timeout debe ser mayor que cero")
    return args


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        run_checks(args.terminal_ip, args.port, args.sample_root, args.timeout)
    except (AssertionError, OSError, RuntimeError, http.client.HTTPException) as error:
        print(f"FAIL {error}", file=sys.stderr)
        return 1
    print("HTTP CHECKS PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
