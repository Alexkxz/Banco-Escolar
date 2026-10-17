"""Host-side contracts for operator-confirmed visual and touch health checks."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
MAIN = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
OTA = (ROOT / "src/network/ota_manager.cpp").read_text(encoding="utf-8")
PROOF = (ROOT / "src/network/ota_boot_validation.h").read_text(encoding="utf-8")


class ProofModel:
    """Synthetic lifecycle model; firmware-source assertions tie its contract to C++."""
    def __init__(self):
        self.boot_id = 0
        self.screen = False
        self.touch = False

    def begin(self, boot_id):
        self.boot_id = boot_id
        self.screen = False
        self.touch = False

    def acknowledge(self, boot_id, channel):
        if not boot_id or boot_id != self.boot_id:
            return False
        if channel == "screen":
            self.screen = True
        elif channel == "touch":
            self.touch = True
        return True

    def complete(self, boot_id):
        return bool(boot_id and boot_id == self.boot_id and self.screen and self.touch)


class OtaVisualValidationContractTests(unittest.TestCase):
    def test_each_pending_boot_starts_with_fresh_nonce_and_empty_proof(self):
        setup = MAIN.split("void setup()", 1)[1].split("void loop()", 1)[0]
        self.assertIn("ota_manager.bootValidationPending()", setup)
        self.assertIn("ota_boot_validation_proof.begin(ota_boot_validation_ui_boot_id)", setup)
        self.assertIn("ota_boot_validation_proof.clear()", setup)
        self.assertIn("esp_random()", setup)
        proof = ProofModel()
        proof.begin(41)
        proof.acknowledge(41, "screen")
        proof.acknowledge(41, "touch")
        self.assertTrue(proof.complete(41))
        proof.begin(42)
        self.assertFalse(proof.complete(42))
        self.assertFalse(proof.acknowledge(41, "screen"))

    def test_health_requires_both_current_boot_operator_checks(self):
        health = MAIN.split("bool ota_boot_health_ok()", 1)[1].split(
            "bool ota_prepare_boot_storage()", 1)[0]
        self.assertIn("ota_boot_validation_proof.complete(ota_boot_validation_ui_boot_id)", health)
        self.assertIn("operator_checks_complete && lv_scr_act() != nullptr", health)
        self.assertIn("ota_health_display == nullptr", health)
        self.assertIn("ota_health_touch == nullptr", health)
        self.assertIn("esp_lv_adapter_get_frame_count() > ota_health_frame_baseline", health)

    def test_pending_screen_requires_explicit_visual_ack_and_real_lvgl_click(self):
        screen = MAIN.split("void create_ota_boot_validation_screen(", 1)[1].split(
            "const char *sd_boot_status_text()", 1)[0]
        self.assertIn("Confirmo que la pantalla se ve correctamente", screen)
        self.assertIn("Tocar aquí para probar el touch", screen)
        self.assertGreaterEqual(screen.count("lv_obj_add_event_cb("), 2)
        self.assertIn("LV_EVENT_CLICKED", MAIN.split("void ota_boot_validation_button_event(", 1)[1].split(
            "void create_ota_boot_validation_screen(", 1)[0])
        self.assertIn("acknowledgeScreen(context->boot_id)", MAIN)
        self.assertIn("verifyTouch(context->boot_id)", MAIN)
        self.assertIn("[OTA-BOOT-CHECK] boot=", MAIN)

    def test_operator_screen_stays_visible_until_validation_finishes(self):
        timer = MAIN.split("void startup_progress_timer_cb(", 1)[1].split(
            "} // namespace", 1)[0]
        self.assertIn("if (ota_boot_validation_screen_active.load()) return;", timer)
        loop = MAIN.split("void loop()", 1)[1]
        self.assertIn("ota_boot_validation_screen_active.store(false)", loop)
        self.assertIn("lv_scr_load(boot_splash_screen)", loop)

    def test_validation_timeout_is_three_minutes_and_still_rolls_back_once(self):
        self.assertIn("ROLLBACK_HEALTH_WINDOW_MS = 180000", OTA)
        self.assertIn("ROLLBACK_DEADLINE_MS = 180000", OTA)
        monitor = OTA.split("void OtaManager::update()", 1)[1].split(
            "bool OtaManager::setBootHealthHook", 1)[0]
        self.assertIn("elapsed >= ROLLBACK_DEADLINE_MS && !rollback_attempted_", monitor)
        reject = OTA.split("bool OtaManager::rejectRunningImageAndRollback()", 1)[1].split(
            "bool OtaManager::confirmReadyToReboot", 1)[0]
        self.assertIn("if (rollback_attempted_) return false", reject)

    def test_proof_rejects_stale_boot_ids_and_requires_both_flags(self):
        self.assertIn("acknowledgeScreen(uint32_t boot_id)", PROOF)
        self.assertIn("verifyTouch(uint32_t boot_id)", PROOF)
        self.assertIn("screen_acknowledged_.store(false", PROOF)
        self.assertIn("touch_verified_.store(false", PROOF)
        self.assertIn("screen_acknowledged_.load", PROOF)
        self.assertIn("touch_verified_.load", PROOF)
        proof = ProofModel()
        proof.begin(99)
        self.assertFalse(proof.complete(99))
        proof.acknowledge(99, "screen")
        self.assertFalse(proof.complete(99))
        proof.acknowledge(99, "touch")
        self.assertTrue(proof.complete(99))
        proof.begin(100)
        self.assertFalse(proof.complete(100))


if __name__ == "__main__":
    unittest.main(verbosity=2)
