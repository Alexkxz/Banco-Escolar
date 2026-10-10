#!/usr/bin/env python3
"""Exercise the live HTTP probe against a temporary local HTTP fixture."""

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import threading
import unittest

from tools.web.test_panel_http_live import DEFAULT_SAMPLE_ROOT, run_checks


class FixtureHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/":
            self._send(200, "text/html; charset=utf-8", (DEFAULT_SAMPLE_ROOT / "index.html").read_bytes())
        elif self.path == "/status":
            version = (DEFAULT_SAMPLE_ROOT / "version.txt").read_text(encoding="ascii").strip()
            self._send(200, "application/json", json.dumps({"sd_mounted": True, "page_version": version}).encode())
        elif self.path == "/panel-test/terminal.svg":
            self._send(200, "image/svg+xml", (DEFAULT_SAMPLE_ROOT / "terminal.svg").read_bytes())
        elif self.path.startswith("/panel-test/__http_probe_missing_"):
            self._send(404, "text/plain", b"File not found")
        elif self.path == "/not-panel-test":
            self._send(400, "text/plain", b"Invalid file route")
        else:
            self._send(404, "text/plain", b"Not found")

    def do_POST(self):
        self._send(405, "text/plain", b"Method not allowed")

    def _send(self, status, content_type, body):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        pass


class LiveHttpClientTests(unittest.TestCase):
    def test_full_probe_suite_against_local_fixture(self):
        server = ThreadingHTTPServer(("127.0.0.1", 0), FixtureHandler)
        server_thread = threading.Thread(target=server.serve_forever, daemon=True)
        server_thread.start()
        reports = []
        try:
            run_checks(
                "127.0.0.1",
                server.server_port,
                DEFAULT_SAMPLE_ROOT,
                timeout=2.0,
                report=reports.append,
            )
            self.assertEqual(len(reports), 6)
            self.assertIn("PASS GET /", reports[0])
            self.assertIn("HTTP 404", reports[3])
            self.assertIn("HTTP 400", reports[4])
            self.assertIn("HTTP 405", reports[5])
        finally:
            server.shutdown()
            server.server_close()
            server_thread.join(timeout=2.0)
        self.assertFalse(server_thread.is_alive(), "fixture server thread did not stop")


if __name__ == "__main__":
    unittest.main(verbosity=2)
