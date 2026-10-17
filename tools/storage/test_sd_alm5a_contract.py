import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANAGER_H = (ROOT / "src/storage/sd_manager.h").read_text(encoding="utf-8")
MANAGER_CPP = (ROOT / "src/storage/sd_manager.cpp").read_text(encoding="utf-8")
DIAG = (ROOT / "src/diagnostics/sd_file_test.cpp").read_text(encoding="utf-8")
SERIAL = (ROOT / "src/diagnostics/terminal_capture.cpp").read_text(encoding="utf-8")


def body(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unclosed body: {signature}")


class SdAlm5aContractTests(unittest.TestCase):
    def test_incremental_writer_is_exclusive_and_checks_sync_and_close(self):
        begin = body(MANAGER_CPP, "SdFileResult SDManager::beginExclusiveStreamWrite(")
        chunk = body(MANAGER_CPP, "SdFileResult SDManager::writeExclusiveStreamChunk(")
        finish = body(MANAGER_CPP, "SdFileResult SDManager::finishExclusiveStreamWrite(")
        self.assertIn("O_WRONLY | O_CREAT | O_EXCL", begin)
        self.assertIn("diagnostic_owner_", begin)
        self.assertIn("bytes_written += static_cast<size_t>(count)", chunk)
        self.assertIn("bytes_written != length", chunk)
        self.assertIn("SD_MAX_EXCLUSIVE_WRITE_CHUNK_BYTES = 4096", MANAGER_CPP)
        self.assertIn("fsync(exclusive_write_descriptor_)", finish)
        self.assertIn("close(exclusive_write_descriptor_)", finish)
        self.assertNotIn("unlink(", begin + chunk + finish)

    def test_exclusive_diagnostic_lease_is_bounded_and_serializes_sd_operations(self):
        enter = body(MANAGER_CPP, "bool SDManager::enterOperation()")
        begin = body(MANAGER_CPP, "bool SDManager::beginDiagnosticExclusive(")
        end = body(MANAGER_CPP, "bool SDManager::endDiagnosticExclusive()")
        drain = body(MANAGER_CPP, "bool SDManager::closeAdmissionAndDrain(")
        self.assertIn("io_owner_.test_and_set", enter)
        self.assertIn("diagnostic_owner", enter)
        self.assertIn("closeAdmissionAndDrain(timeout_ms)", begin)
        self.assertIn("operation_gate_.store(1", begin)
        self.assertIn("fetch_sub(1", end)
        self.assertIn("millis() - start", drain)
        http = (ROOT / "src/network/panel_http_server.cpp").read_text(encoding="utf-8")
        self.assertIn("if (server_started) server.handleClient()", http)
        self.assertIn('stream_result.error == SdFileError::CLOSING) send_error(503, "microSD is busy")', http)

    def test_manual_commands_are_gated_and_large_test_is_capped(self):
        self.assertIn('strcmp(line, "SD DIAG VERIFY")', SERIAL)
        self.assertIn('strcmp(line, "SD DIAG LARGE")', SERIAL)
        self.assertIn("sd_file_verify_test_run();", SERIAL)
        self.assertIn("sd_file_large_test_run();", SERIAL)
        self.assertIn("1024U * 1024U", DIAG)
        self.assertIn("4096", DIAG)
        self.assertIn("beginDiagnosticExclusive(10000)", DIAG)
        self.assertIn("xTaskCreate(sd_diagnostic_task", SERIAL)
        self.assertIn("s_sd_diagnostic_task_active", SERIAL)
        self.assertIn("DIAG.1 ABORTADO: termina primero el diagnóstico avanzado", SERIAL)

    def test_manifest_is_persisted_and_incomplete_or_corrupt_records_fail(self):
        parse = body(DIAG, "bool parse_manifest(")
        verify = body(DIAG, "void sd_file_verify_test_run()")
        large = body(DIAG, "void sd_file_large_test_run()")
        self.assertIn("ALM5A1|", parse)
        self.assertIn("stored_record_crc", parse)
        self.assertIn("createFileExclusive(manifest_path", large)
        self.assertIn("run_", large)
        self.assertIn("registro incompleto/corrupto", verify)
        self.assertIn("sin registro persistente verificable", verify)
        self.assertIn("archivo parcial/sin manifiesto", verify)
        self.assertIn("streamFile(path", DIAG)

    def test_metrics_and_failure_paths_are_reported_without_cleanup(self):
        large = body(DIAG, "void sd_file_large_test_run()")
        self.assertIn("writeExclusiveStreamChunk", large)
        self.assertIn("RESULTADO", large)
        self.assertIn("bytes_per_second", large)
        self.assertIn("memory_before", large)
        self.assertIn("archivo parcial conservado", large)
        self.assertNotIn("removeFile(", large)
        self.assertNotIn("unlink(", large)


if __name__ == "__main__":
    unittest.main(verbosity=2)
