#!/usr/bin/env python3
"""Validate and stage the current Panel Maestro build for the read-only SD server."""

from __future__ import annotations

import hashlib
import argparse
import json
import re
import shutil
import sys
from html.parser import HTMLParser
from pathlib import Path, PurePosixPath
from urllib.parse import urlsplit


ROOT = Path(__file__).resolve().parents[2]
PANEL = ROOT / "panel-maestro"
BUILD = PANEL / "dist-sd"
PACKAGE = PANEL / "package-sd"
SD_PREFIX = "panel-test/maestro"
PUBLIC_BASE = f"/{SD_PREFIX}/"


class HtmlReferences(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.urls: list[str] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        for name, value in attrs:
            if name in {"src", "href"} and value:
                self.urls.append(value)


def fail(message: str) -> "NoReturn":
    raise RuntimeError(message)


def clean_manifest_path(value: str) -> str:
    path = PurePosixPath(value)
    if path.is_absolute() or ".." in path.parts or not path.parts:
        fail(f"Ruta insegura en el manifiesto Vite: {value}")
    return path.as_posix()


def read_vite_manifest() -> tuple[Path, dict[str, dict[str, object]]]:
    candidates = [BUILD / ".vite" / "manifest.json", BUILD / "manifest.json"]
    manifest_path = next((path for path in candidates if path.is_file()), None)
    if manifest_path is None:
        fail("No existe el manifiesto de Vite. Ejecuta primero `npm run build:sd`.")
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"No se pudo leer el manifiesto Vite: {error}")
    if not isinstance(manifest, dict) or "index.html" not in manifest:
        fail("El manifiesto Vite no contiene la entrada index.html.")
    return manifest_path, manifest


def collect_build_files(manifest: dict[str, dict[str, object]]) -> set[str]:
    files: set[str] = {"index.html"}
    visited: set[str] = set()

    def visit(key: str) -> None:
        if key in visited:
            return
        visited.add(key)
        chunk = manifest.get(key)
        if chunk is None:
            fail(f"Dependencia ausente del manifiesto Vite: {key}")
        filename = chunk.get("file")
        if not isinstance(filename, str):
            fail(f"Entrada Vite sin archivo: {key}")
        files.add(clean_manifest_path(filename))
        for collection in ("css", "assets"):
            values = chunk.get(collection, [])
            if isinstance(values, list):
                for value in values:
                    if isinstance(value, str):
                        files.add(clean_manifest_path(value))
        for collection in ("imports", "dynamicImports"):
            values = chunk.get(collection, [])
            if isinstance(values, list):
                for value in values:
                    if isinstance(value, str):
                        visit(value)

    visit("index.html")
    return files


def resolve_html_reference(url: str, expected_files: set[str]) -> str | None:
    parsed = urlsplit(url)
    if parsed.scheme or parsed.netloc or url.startswith("#") or url.startswith("data:"):
        return None
    path = parsed.path
    if not path:
        return None
    if path.startswith(PUBLIC_BASE):
        relative = path[len(PUBLIC_BASE):]
    elif not path.startswith("/"):
        relative = path
    else:
        fail(f"Referencia local fuera de {PUBLIC_BASE}: {url}")
    relative = clean_manifest_path(relative)
    if relative not in expected_files:
        fail(f"Referencia HTML no está en el paquete: {url}")
    return relative


def validate_build(build_files: set[str]) -> None:
    missing = sorted(path for path in build_files if not (BUILD / Path(*PurePosixPath(path).parts)).is_file())
    if missing:
        fail("Faltan archivos emitidos por Vite: " + ", ".join(missing))

    parser = HtmlReferences()
    parser.feed((BUILD / "index.html").read_text(encoding="utf-8"))
    for url in parser.urls:
        resolve_html_reference(url, build_files)

    css_url_pattern = re.compile(r"url\(\s*(['\"]?)(.*?)\1\s*\)", re.IGNORECASE)
    for filename in sorted(build_files):
        if not filename.endswith(".css"):
            continue
        css = (BUILD / Path(*PurePosixPath(filename).parts)).read_text(encoding="utf-8")
        for _, url in css_url_pattern.findall(css):
            url = url.strip()
            parsed = urlsplit(url)
            if parsed.scheme or parsed.netloc or url.startswith("#") or not parsed.path:
                continue
            if parsed.path.startswith(PUBLIC_BASE):
                relative = parsed.path[len(PUBLIC_BASE):]
            elif parsed.path.startswith("/"):
                fail(f"Recurso CSS fuera de {PUBLIC_BASE}: {url}")
            else:
                relative = (PurePosixPath(filename).parent / parsed.path).as_posix()
            relative = clean_manifest_path(relative)
            if relative not in build_files:
                fail(f"Recurso CSS no está en el paquete: {url} ({filename})")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def safe_reset_package() -> None:
    expected = (PANEL / "package-sd").resolve()
    resolved = PACKAGE.resolve()
    if resolved != expected or ROOT.resolve() not in resolved.parents:
        fail(f"El directorio de salida no coincide con la ruta autorizada: {resolved}")
    if PACKAGE.is_symlink():
        fail("La ruta de salida package-sd no puede ser un enlace simbólico.")
    if PACKAGE.exists():
        shutil.rmtree(PACKAGE)
    (PACKAGE / SD_PREFIX).mkdir(parents=True)


def verify_copy(root: Path) -> None:
    manifest_path = PACKAGE / "manifest.json"
    if not manifest_path.is_file():
        fail("No existe package-sd/manifest.json; prepara el paquete antes de verificarlo.")
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"No se pudo leer el manifiesto del paquete: {error}")
    if manifest.get("format") != "BancoEscolarPanelSdPackage" or manifest.get("schema_version") != 1:
        fail("Formato de manifiesto no reconocido.")
    records = manifest.get("files")
    if not isinstance(records, list) or not records:
        fail("El manifiesto no contiene la lista de archivos del paquete.")

    expected: set[str] = set()
    for record in records:
        if not isinstance(record, dict):
            fail("Registro inválido en el manifiesto.")
        relative = clean_manifest_path(str(record.get("path", "")))
        if not relative.startswith(SD_PREFIX + "/"):
            fail(f"Ruta fuera del paquete esperada: {relative}")
        expected.add(relative)
        path = root / Path(*PurePosixPath(relative).parts)
        if not path.is_file():
            fail(f"Falta archivo en la copia: {path}")
        if path.stat().st_size != record.get("size_bytes"):
            fail(f"Tamaño distinto para {relative}")
        if sha256(path) != record.get("sha256"):
            fail(f"SHA-256 distinto para {relative}")

    copied_prefix = root / Path(*PurePosixPath(SD_PREFIX).parts)
    actual = {
        path.relative_to(root).as_posix()
        for path in copied_prefix.rglob("*")
        if path.is_file()
    } if copied_prefix.is_dir() else set()
    if actual != expected:
        extra = sorted(actual - expected)
        missing = sorted(expected - actual)
        fail(f"Contenido distinto al manifiesto (extras={extra}, faltantes={missing}).")
    print(f"Copia verificada: {len(expected)} archivos coinciden en {root}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--verify-root",
        type=Path,
        help="verifica una copia usando package-sd/manifest.json; la raíz debe contener panel-test/maestro/",
    )
    args = parser.parse_args()
    if args.verify_root is not None:
        verify_copy(args.verify_root.resolve())
        return 0

    if not BUILD.is_dir():
        fail("No existe panel-maestro/dist-sd. Ejecuta primero `npm run build:sd`.")
    _, vite_manifest = read_vite_manifest()
    build_files = collect_build_files(vite_manifest)
    validate_build(build_files)
    safe_reset_package()

    for relative in sorted(build_files):
        source = BUILD / Path(*PurePosixPath(relative).parts)
        target = PACKAGE / SD_PREFIX / Path(*PurePosixPath(relative).parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)

    records = []
    for relative in sorted(build_files):
        package_relative = f"{SD_PREFIX}/{relative}"
        path = PACKAGE / Path(*PurePosixPath(package_relative).parts)
        records.append({"path": package_relative, "size_bytes": path.stat().st_size, "sha256": sha256(path)})

    manifest = {
        "format": "BancoEscolarPanelSdPackage",
        "schema_version": 1,
        "entry_url_path": f"/{SD_PREFIX}/index.html#/dashboard",
        "sd_copy_target": f"/{SD_PREFIX}/",
        "files": records,
    }
    (PACKAGE / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n"
    )
    print(f"Paquete preparado: {PACKAGE.relative_to(ROOT)}")
    print(f"Archivos de aplicación: {len(records)}")
    print(f"Manifiesto SHA-256: {PACKAGE.relative_to(ROOT) / 'manifest.json'}")
    for record in records:
        print(f"{record['size_bytes']:>9} B  {record['sha256']}  {record['path']}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
