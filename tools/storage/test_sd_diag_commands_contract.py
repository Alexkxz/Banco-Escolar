import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANAGER = (ROOT / "src/storage/sd_manager.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "src/storage/sd_manager.h").read_text(encoding="utf-8")
DIAGNOSTIC = (ROOT / "src/diagnostics/sd_file_test.cpp").read_text(encoding="utf-8")
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


class SdDiagCommandContractTests(unittest.TestCase):
    def test_info_is_manual_read_only_and_reports_available_metadata(self):
        info = body(DIAGNOSTIC, "void sd_file_info_test_run()")
        self.assertIn('strcmp(line, "SD DIAG INFO")', SERIAL)
        self.assertIn("sd_file_info_test_run();", SERIAL)
        for token in ("cardType()", "cardSectorCount()", "cardSectorSizeBytes()",
                      "cardCapacityBytes()", "filesystemTypeCode()",
                      "filesystemStatsAvailable()", "totalBytes()", "freeBytes()"):
            self.assertIn(token, info)
        for forbidden in ("createDirectory(", "createFileExclusive(", "writeFile(",
                          "removeFile(", "rename(", "unlink(", "format("):
            self.assertNotIn(forbidden, info)
        self.assertIn("NO DISPONIBLE", info)

    def test_exclusive_create_cannot_truncate_or_recover_existing_paths(self):
        create = body(MANAGER, "SdFileResult SDManager::createFileExclusive(")
        self.assertIn("SdFileResult createFileExclusive(", HEADER)
        self.assertIn("O_WRONLY | O_CREAT | O_EXCL", create)
        self.assertNotIn("recoverWrite(", create)
        self.assertNotIn("unlink(", create)
        self.assertNotIn("rename(", create)
        self.assertIn("fsync(descriptor)", create)
        self.assertIn("close(descriptor)", create)
        self.assertIn("if (count < 0 && errno == EINTR) continue", create)

    def test_write_command_preserves_created_file_and_checks_exact_data(self):
        write = body(DIAGNOSTIC, "void sd_file_write_test_run()")
        self.assertIn('strcmp(line, "SD DIAG WRITE")', SERIAL)
        self.assertIn("createDirectory(WRITE_TEST_DIRECTORY)", write)
        self.assertIn("createFileExclusive(relative_path", write)
        self.assertIn("esp_random()", write)
        self.assertIn("sd_manager.readFile(relative_path, contents)", write)
        self.assertIn("memcmp(contents.data(), WRITE_TEST_CONTENT", write)
        self.assertIn("diag_capture::crc32", write)
        self.assertIn("RESULTADO: PASS", write)
        self.assertIn("RESULTADO: FALLO", write)
        for forbidden in ("writeFile(", "removeFile(", "rename(", "unlink(", "format("):
            self.assertNotIn(forbidden, write)


if __name__ == "__main__":
    unittest.main(verbosity=2)
