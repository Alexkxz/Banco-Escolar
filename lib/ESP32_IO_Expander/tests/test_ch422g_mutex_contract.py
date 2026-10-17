from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
PORT = ROOT / "src" / "port"
INTERNAL = ROOT / "internal"
DRIVERS = (
    "esp_io_expander_ch422g.c",
    "esp_io_expander_tca9554.c",
    "esp_io_expander_tca95xx_16bit.c",
    "esp_io_expander_ht8574.c",
)


class Ch422gMutexContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.generic = (PORT / "esp_io_expander.c").read_text(encoding="utf-8")
        cls.ch422g = (PORT / "esp_io_expander_ch422g.c").read_text(encoding="utf-8")
        cls.lock = (INTERNAL / "esp_io_expander_lock.h").read_text(encoding="utf-8")
        cls.header = (PORT / "esp_io_expander.h").read_text(encoding="utf-8")
        cls.private = (INTERNAL / "esp_io_expander_private.h").read_text(encoding="utf-8")
        cls.unity = (ROOT / "test_apps" / "main" / "test_chip_general.cpp").read_text(encoding="utf-8")

    def test_mutex_is_per_ch422g_handle_and_finite(self):
        self.assertIn("SemaphoreHandle_t operation_mutex", self.private)
        self.assertIn("xSemaphoreCreateRecursiveMutex()", self.ch422g)
        self.assertIn("ESP_IO_EXPANDER_OPERATION_TIMEOUT_MS (50)", self.lock)
        self.assertIn("xSemaphoreTakeRecursive", self.lock)
        self.assertIn("? ESP_OK : ESP_ERR_TIMEOUT", self.lock)

    def test_generic_apis_admit_before_mutex_and_end_after_operation(self):
        for public, inner in (
            ("esp_io_expander_set_dir", "set_dir_unlocked"),
            ("esp_io_expander_set_level", "set_level_unlocked"),
            ("esp_io_expander_get_level", "get_level_unlocked"),
            ("esp_io_expander_print_state", "print_state_unlocked"),
            ("esp_io_expander_reset", "reset_unlocked"),
        ):
            with self.subTest(api=public):
                start = self.generic.index(f"{public}(", self.generic.index("esp_io_expander_del("))
                body = self.generic[start:self.generic.find("\n}", start) + 2]
                self.assertIn("esp_io_expander_operation_begin(handle)", body)
                self.assertIn(f"{inner}(handle", body)
                self.assertIn("esp_io_expander_operation_end(handle)", body)

    def test_ch422g_exports_and_register_callbacks_are_guarded(self):
        for name in (
            "esp_io_expander_ch422g_set_oc_open_drain",
            "esp_io_expander_ch422g_set_oc_push_pull",
            "esp_io_expander_ch422g_set_all_input",
            "esp_io_expander_ch422g_set_all_output",
        ):
            with self.subTest(api=name):
                start = self.ch422g.index(f"esp_err_t {name}(")
                body = self.ch422g[start:self.ch422g.find("\n}", start) + 2]
                self.assertIn("esp_io_expander_operation_begin(handle)", body)
                self.assertIn("esp_io_expander_operation_end(handle)", body)

        for name in ("read_input_reg", "write_output_reg", "read_output_reg",
                     "write_direction_reg", "read_direction_reg", "reset"):
            with self.subTest(callback=name):
                start = self.ch422g.rfind(f"static esp_err_t {name}(")
                body = self.ch422g[start:self.ch422g.find("\n}", start) + 2]
                self.assertIn("esp_io_expander_operation_lock(handle)", body)
                self.assertIn("esp_io_expander_operation_unlock(handle)", body)

    def test_lifecycle_counts_waiters_rejects_close_and_cancels_timeout(self):
        begin = self.lock[self.lock.index("esp_io_expander_operation_begin("):]
        self.assertLess(begin.index("++handle->admitted_operations"), begin.index("esp_err_t ret = esp_io_expander_operation_lock(handle)"))
        self.assertIn("ESP_IO_EXPANDER_LIFECYCLE_CLOSING", begin)
        self.assertIn("ESP_ERR_INVALID_STATE", begin)
        delete = self.generic[self.generic.index("esp_err_t esp_io_expander_del("):]
        self.assertIn("handle->lifecycle_state = ESP_IO_EXPANDER_LIFECYCLE_CLOSING", delete)
        self.assertIn("ESP_ERR_TIMEOUT", delete)
        self.assertIn("handle->lifecycle_state = ESP_IO_EXPANDER_LIFECYCLE_ACTIVE", delete)
        self.assertIn("handle->admitted_operations", delete)

    def test_driver_callbacks_are_absent_from_public_handle(self):
        self.assertNotIn("struct esp_io_expander_s {", self.header)
        self.assertIn("struct esp_io_expander_s {", self.private)
        self.assertNotIn("(*del)", self.header)
        for name in DRIVERS:
            source = (PORT / name).read_text(encoding="utf-8")
            self.assertIn('"../../internal/esp_io_expander_private.h"', source)

    def test_host_unity_cases_cover_contention_timeout_and_error_release(self):
        self.assertIn("preserve neighboring output bits under concurrency", self.unity)
        self.assertIn("mutex timeout is finite", self.unity)
        self.assertIn("releases mutex after read and write errors", self.unity)
        self.assertIn("close drains active and mutex-waiting operations", self.unity)
        self.assertIn("close timeout cancels without destroying resources", self.unity)


if __name__ == "__main__":
    unittest.main()
