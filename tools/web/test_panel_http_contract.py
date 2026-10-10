#!/usr/bin/env python3
"""Hardware-independent source contract checks for the SD HTTP server."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SERVER = (ROOT / "src/network/panel_http_server.cpp").read_text(encoding="utf-8")
SD_MANAGER = (ROOT / "src/storage/sd_manager.cpp").read_text(encoding="utf-8")
SD_HEADER = (ROOT / "src/storage/sd_manager.h").read_text(encoding="utf-8")
MAIN = (ROOT / "src/main.cpp").read_text(encoding="utf-8")


def body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    raise AssertionError(f"unclosed function: {signature}")


class PanelHttpContractTests(unittest.TestCase):
    def test_missing_file_returns_404(self):
        handler = body(SERVER, "void handle_file()")
        self.assertIn("sd_manager.streamFile(", handler)
        self.assertIn("SdFileError::NOT_FOUND) send_error(404", handler)

    def test_invalid_route_and_path_return_400(self):
        resolver = body(SERVER, "bool resolve_panel_path(")
        handler = body(SERVER, "void handle_file()")
        self.assertIn('uri.startsWith("/panel-test/")', resolver)
        self.assertIn("requested.indexOf('%')", resolver)
        self.assertIn("requested.indexOf('\\\\')", resolver)
        self.assertIn("if (!resolve_panel_path(server.uri(), relative_path))", handler)
        self.assertIn("send_error(400, \"Invalid file route\")", handler)
        self.assertIn("SdFileError::INVALID_PATH) send_error(400", handler)

    def test_unmounted_sd_returns_503_and_status_remains_available(self):
        handler = body(SERVER, "void handle_file()")
        status = body(SERVER, "void handle_status()")
        self.assertIn("if (!sd_manager.isReady())", handler)
        self.assertIn("send_error(503, \"microSD is not mounted\")", handler)
        self.assertIn('server.on("/status", HTTP_GET, handle_status)', SERVER)
        self.assertIn('"sd_mounted\\":%s', status)
        self.assertIn('"page_version\\":\\"%s\\"', status)

    def test_file_reads_are_streamed_and_not_buffered_as_a_whole(self):
        self.assertIn("SdStreamCallback", SD_HEADER)
        stream = body(SD_MANAGER, "SdFileResult SDManager::streamFile(")
        self.assertIn("uint8_t buffer[1024]", stream)
        self.assertIn("fread(buffer, 1, sizeof(buffer), file)", stream)
        self.assertIn("on_chunk(context, buffer, count)", stream)
        self.assertNotIn("std::vector", stream)
        self.assertNotIn("recoverWrite", stream)
        self.assertIn("panel_http_server.update()", MAIN)

    def test_http_reads_do_not_call_mutating_sd_operations(self):
        self.assertNotIn("fileSize(", SERVER)
        self.assertNotIn("readFile(", SERVER)
        self.assertNotIn("writeFile(", SERVER)
        self.assertNotIn("removeFile(", SERVER)
        self.assertNotIn("createDirectory(", SERVER)
        self.assertNotIn("recoverWrite", body(SD_MANAGER, "SdFileResult SDManager::streamFile("))

    def test_server_only_accepts_get_and_head_for_files(self):
        handler = body(SERVER, "void handle_file()")
        self.assertIn("server.method() != HTTP_GET && server.method() != HTTP_HEAD", handler)
        self.assertIn('server.sendHeader("Allow", "GET, HEAD")', handler)
        self.assertIn("send_error(405, \"Method not allowed\")", handler)
        self.assertNotIn("HTTP_POST", SERVER)
        self.assertNotIn("HTTP_PUT", SERVER)
        self.assertNotIn("HTTP_DELETE", SERVER)


if __name__ == "__main__":
    unittest.main(verbosity=2)
