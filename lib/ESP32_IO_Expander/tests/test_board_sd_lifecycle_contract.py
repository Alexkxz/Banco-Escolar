from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
BOARD_HPP = ROOT / "ESP32_Display_Panel/src/board/esp_panel_board.hpp"
BOARD_CPP = ROOT / "ESP32_Display_Panel/src/board/esp_panel_board.cpp"
ADAPTER = ROOT / "ESP32_Display_Panel/src/drivers/io_expander/esp_panel_io_expander_adapter.hpp"
SD_HPP = ROOT.parent / "src/storage/sd_manager.h"
SD_CPP = ROOT.parent / "src/storage/sd_manager.cpp"
MAIN_CPP = ROOT.parent / "src/main.cpp"


class BoardSdLifecycleContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.board_hpp = BOARD_HPP.read_text(encoding="utf-8")
        cls.board_cpp = BOARD_CPP.read_text(encoding="utf-8")
        cls.adapter = ADAPTER.read_text(encoding="utf-8")
        cls.sd_hpp = SD_HPP.read_text(encoding="utf-8")
        cls.sd_cpp = SD_CPP.read_text(encoding="utf-8")
        cls.main = MAIN_CPP.read_text(encoding="utf-8")

    def test_sd_manager_retains_board_expander_and_hooks_bracket_board_delete(self):
        self.assertIn("getIO_ExpanderShared() const", self.board_hpp)
        self.assertIn("_owns_io_expander ? _io_expander : nullptr", self.board_hpp)
        self.assertIn("std::shared_ptr<esp_panel::drivers::IO_Expander> expander_owner_", self.sd_hpp)
        self.assertIn("sd_manager.shutdownForBoardDelete()", self.main)
        self.assertIn("sd_manager.completeBoardDelete()", self.main)
        self.assertIn("board->getIO_ExpanderShared()", self.main)

    def test_board_preserves_controller_for_retry_and_closes_before_drop(self):
        delete = self.board_cpp[self.board_cpp.index("bool Board::del()"):]
        self.assertLess(delete.index("STAGE_CALLBACK_PRE_BOARD_DEL"), delete.index("_io_expander->del()"))
        self.assertLess(delete.index("_io_expander->del()"), delete.index("_io_expander = nullptr"))
        self.assertIn("ESP_UTILS_CHECK_FALSE_RETURN(_io_expander->del()", delete)
        self.assertIn("_owns_io_expander", self.board_cpp)

    def test_adapter_closes_controller_before_host_and_retains_host_on_error(self):
        delete = self.adapter[self.adapter.index("bool IO_ExpanderAdapter<T>::del()"):]
        self.assertLess(delete.index("T::del()"), delete.index("HostI2C::tryReleaseInstance"))
        self.assertLess(delete.index("HostI2C::tryReleaseInstance"), delete.index("_host = nullptr"))
        self.assertIn("ESP_UTILS_CHECK_FALSE_RETURN", delete)

    def test_gate_counts_admitted_callbacks_and_has_finite_retryable_close(self):
        self.assertIn("compare_exchange_weak", self.sd_cpp)
        self.assertIn("operation_gate_.fetch_or(OPERATION_CLOSING_BIT", self.sd_cpp)
        self.assertIn("SD_CLOSE_DRAIN_TIMEOUT_MS = 10000", self.sd_cpp)
        self.assertIn("vTaskDelay(1)", self.sd_cpp)
        self.assertIn("operation_gate_.fetch_and(OPERATION_COUNT_MASK", self.sd_cpp)
        self.assertIn("return file_error(SdFileError::CLOSING)", self.sd_cpp)
        self.assertIn("set_external_sd_cs, expander_", self.sd_cpp)
        self.assertIn("OperationGuard operation(*this)", self.sd_cpp)

    def test_sd_teardown_preserves_live_context_on_release_errors_and_retries(self):
        teardown = self.sd_cpp[self.sd_cpp.index("bool SDManager::unmountImpl()"):]
        release = self.sd_cpp[self.sd_cpp.index("bool SDManager::releaseBus()"):]
        self.assertLess(release.index("return false;"), release.index("SPI.end()"))
        self.assertIn("if (!releaseBus())", teardown)
        self.assertIn("expander_owner_.reset()", self.sd_cpp)
        self.assertIn("board_shutdown_complete_", self.sd_cpp)
        self.assertIn("sdcard_uninit unregisters the disk and frees its card object", self.sd_cpp)


if __name__ == "__main__":
    unittest.main()
