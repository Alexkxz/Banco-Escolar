"""Host-side contracts for fail-closed OTA boot validation ordering."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
OTA_CPP = (ROOT / "src/network/ota_manager.cpp").read_text(encoding="utf-8")
OTA_H = (ROOT / "src/network/ota_manager.h").read_text(encoding="utf-8")
MAIN = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
STORAGE = (ROOT / "src/storage/storage_manager.cpp").read_text(encoding="utf-8")
LITTLEFS = (ROOT / "src/storage/bank_littlefs.cpp").read_text(encoding="utf-8")
CONFIG = (ROOT / "src/network/ota_config.h").read_text(encoding="utf-8")


class OtaBootValidationContractTests(unittest.TestCase):
    def test_state_query_error_is_unknown_and_requires_maintenance(self):
        begin = OTA_CPP.split("bool OtaManager::begin()", 1)[1].split(
            "bool OtaManager::bootValidationPending()", 1)[0]
        hold = OTA_CPP.split("bool OtaManager::bootValidationHoldRequired()", 1)[1].split(
            "OtaBootValidationState OtaManager::bootValidationState()", 1)[0]
        setup = MAIN.split("void setup()", 1)[1].split("void loop()", 1)[0]
        self.assertIn("OtaBootValidationState::STATE_UNKNOWN", begin)
        self.assertIn("boot_validation_error_ != ESP_OK", begin)
        self.assertIn("boot_validation_state_ != OtaBootValidationState::NOT_PENDING", hold)
        self.assertIn("!ota_manager_ready_at_boot || ota_manager.bootValidationHoldRequired()", setup)
        self.assertIn("sin confirmar ni reiniciar automáticamente", setup)

    def test_synthetic_pending_and_valid_states_are_distinct(self):
        begin = OTA_CPP.split("bool OtaManager::begin()", 1)[1].split(
            "bool OtaManager::bootValidationPending()", 1)[0]
        self.assertIn("image_state == ESP_OTA_IMG_PENDING_VERIFY", begin)
        self.assertIn("OtaBootValidationState::PENDING_VERIFY", begin)
        self.assertIn("OtaBootValidationState::NOT_PENDING", begin)
        self.assertIn("STATE_UNKNOWN", OTA_H)

    def test_normal_profile_accepts_undefined_only_after_ota0_and_layout_checks(self):
        begin = OTA_CPP.split("bool OtaManager::begin()", 1)[1].split(
            "bool OtaManager::bootValidationPending()", 1)[0]
        geometry = OTA_CPP.split("bool normal_profile_has_expected_layout(", 1)[1].split(
            "bool https_url(", 1)[0]
        undefined_branch = begin.split("image_state == ESP_OTA_IMG_UNDEFINED", 1)[1].split(
            "image_state == ESP_OTA_IMG_NEW", 1)[0]
        self.assertIn("image_state == ESP_OTA_IMG_UNDEFINED", begin)
        self.assertIn("!BANCO_OTA_ENABLED", undefined_branch)
        self.assertIn("ESP_PARTITION_SUBTYPE_APP_OTA_0", undefined_branch)
        self.assertIn("same_partition(selected, running)", undefined_branch)
        self.assertIn("normal_profile_has_expected_layout(running)", undefined_branch)
        self.assertIn("OtaBootValidationState::NOT_PENDING", undefined_branch)
        self.assertIn("OtaBootValidationState::STATE_UNSAFE", undefined_branch)
        for required in ("OTA0_ADDRESS", "OTA1_ADDRESS", "OTADATA_ADDRESS", "FILESYSTEM_ADDRESS",
                         "COREDUMP_ADDRESS", "count == 6"):
            self.assertIn(required, geometry)
        self.assertIn("ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr", geometry)
        self.assertIn("ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr", geometry)
        self.assertNotIn('ESP_PARTITION_SUBTYPE_APP_OTA_0, "ota_0"', geometry)
        self.assertNotIn('ESP_PARTITION_SUBTYPE_APP_OTA_1, "ota_1"', geometry)

    def test_query_errors_unsafe_states_and_pending_remain_fail_closed(self):
        begin = OTA_CPP.split("bool OtaManager::begin()", 1)[1].split(
            "bool OtaManager::bootValidationPending()", 1)[0]
        hold = OTA_CPP.split("bool OtaManager::bootValidationHoldRequired()", 1)[1].split(
            "OtaBootValidationState OtaManager::bootValidationState()", 1)[0]
        self.assertIn("boot_validation_error_ != ESP_OK", begin)
        self.assertIn("OtaBootValidationState::STATE_UNKNOWN", begin)
        self.assertIn("OtaBootValidationState::STATE_UNSAFE", begin)
        for unsafe in ("ESP_OTA_IMG_NEW", "ESP_OTA_IMG_INVALID", "ESP_OTA_IMG_ABORTED"):
            self.assertIn(unsafe, begin)
        self.assertIn("image_state == ESP_OTA_IMG_PENDING_VERIFY", begin)
        self.assertIn("OtaBootValidationState::PENDING_VERIFY", begin)
        self.assertIn("boot_validation_state_ != OtaBootValidationState::NOT_PENDING", hold)

    def test_safe_normal_state_releases_common_gate_used_by_wifi_demo_and_school_operations(self):
        hold = OTA_CPP.split("bool OtaManager::bootValidationHoldRequired()", 1)[1].split(
            "OtaBootValidationState OtaManager::bootValidationState()", 1)[0]
        main = MAIN.split("void loop()", 1)[1]
        wifi = (ROOT / "src/network/wifi_manager.cpp").read_text(encoding="utf-8")
        self.assertIn("return boot_validation_state_ != OtaBootValidationState::NOT_PENDING", hold)
        self.assertIn("if (!ota_maintenance_active.load())", main)
        self.assertIn("if (ota_maintenance_active.load()) return;", MAIN)
        self.assertIn("ota_maintenance_active", wifi)
        # The patch must not alter financial/storage transaction implementations.
        self.assertNotIn("ACCOUNT_TXN_FILE", OTA_CPP)

    def test_confirmation_follows_storage_preparation_and_second_health_check(self):
        update = OTA_CPP.split("void OtaManager::update()", 1)[1].split(
            "bool OtaManager::setBootHealthHook", 1)[0]
        self.assertLess(update.index("boot_storage_prepare_hook_()"), update.index("boot_health_hook_()", update.index("boot_storage_prepare_hook_()")))
        self.assertLess(update.index("boot_health_hook_()", update.index("boot_storage_prepare_hook_()")),
                        update.index("confirmRunningImageHealthy()"))

    def test_synthetic_mount_failure_rejects_without_confirmation(self):
        update = OTA_CPP.split("void OtaManager::update()", 1)[1].split(
            "bool OtaManager::setBootHealthHook", 1)[0]
        failure = update.split("if (boot_storage_prepare_hook_ == nullptr || !boot_storage_prepare_hook_())", 1)[1].split(
            "// Keep the school-data gate", 1)[0]
        self.assertIn("rejectRunningImageAndRollback()", failure)
        self.assertNotIn("confirmRunningImageHealthy()", failure)
        self.assertIn("boot_validation_error_ = ESP_FAIL", failure)

    def test_mount_failure_falls_back_to_read_only_and_does_not_grow(self):
        remount = LITTLEFS.split("bool BankLittleFS::remountReadWrite", 1)[1].split(
            "bool BankLittleFS::isMounted", 1)[0]
        prepare = STORAGE.split("bool StorageManager::prepareFilesystemForBootConfirmation()", 1)[1].split(
            "bool StorageManager::isReady()", 1)[0]
        self.assertIn("mountInternal(false, base_path_, partition_label_, false, true)", remount)
        self.assertIn("mountInternal(true, base_path_, partition_label_, false, false)", remount)
        self.assertIn("remountReadWriteForBootValidation()", prepare)

    def test_pending_validation_never_unlocks_before_mount_and_record_checks(self):
        prepare = STORAGE.split("bool StorageManager::prepareFilesystemForBootConfirmation()", 1)[1].split(
            "bool StorageManager::isReady()", 1)[0]
        loop = MAIN.split("void loop()", 1)[1]
        self.assertLess(prepare.index("remountReadWriteForBootValidation()"),
                        prepare.index("validateCompleteMovementRecords()"))
        self.assertIn("ota_maintenance_active.load()", prepare)
        self.assertLess(loop.index("ota_manager.update()"),
                        loop.index("ota_maintenance_active.store(false)"))

    def test_pending_journal_or_inconsistent_records_reject_image(self):
        begin = STORAGE.split("bool StorageManager::begin()", 1)[1].split(
            "bool StorageManager::prepareFilesystemForBootConfirmation()", 1)[0]
        self.assertIn("pending_transaction_status != BankLittleFsPathStatus::MISSING", begin)
        self.assertIn("!validateCompleteMovementRecords()", begin)
        self.assertIn("!validateAccountSnapshots()", begin)
        prepare = STORAGE.split("bool StorageManager::prepareFilesystemForBootConfirmation()", 1)[1].split(
            "bool StorageManager::isReady()", 1)[0]
        self.assertIn("!records_valid", prepare)
        self.assertIn("recovery_required_ = true", prepare)

    def test_confirmation_api_failure_keeps_pending_and_maintenance_lock(self):
        confirm = OTA_CPP.split("bool OtaManager::confirmRunningImageHealthy()", 1)[1].split(
            "bool OtaManager::rejectRunningImageAndRollback()", 1)[0]
        update = OTA_CPP.split("void OtaManager::update()", 1)[1].split(
            "bool OtaManager::setBootHealthHook", 1)[0]
        self.assertIn("if (boot_validation_error_ != ESP_OK)", confirm)
        mark = confirm.index("boot_validation_error_ = esp_ota_mark_app_valid_cancel_rollback()")
        failure_return = confirm.index("return false", confirm.index("if (boot_validation_error_ != ESP_OK)", mark))
        state_commit = confirm.index("boot_validation_state_ = OtaBootValidationState::NOT_PENDING", mark)
        self.assertLess(failure_return, state_commit)
        self.assertIn("confirmRunningImageHealthy()", update)
        self.assertIn("boot_validation_state_ != OtaBootValidationState::NOT_PENDING", OTA_CPP)

    def test_normal_boot_keeps_rw_mount_and_pending_unknown_states_stay_read_only(self):
        mount = STORAGE.split("bool mountOrInitializeFilesystem()", 1)[1].split(
            "bool findStorageTestRecord", 1)[0]
        self.assertIn("bank_littlefs.mount(validating_ota_image", mount)
        self.assertIn("!validating_ota_image", mount)
        self.assertIn("if (validating_ota_image)", mount)
        self.assertIn("formato bloqueado", mount)

    def test_write_gate_stays_closed_through_mount_validation_and_confirmation(self):
        prepare = MAIN.split("bool ota_prepare_boot_storage()", 1)[1].split("void boot_progress_animation_exec", 1)[0]
        setup = MAIN.split("void setup()", 1)[1].split("void loop()", 1)[0]
        loop = MAIN.split("void loop()", 1)[1]
        self.assertIn("ota_maintenance_active.load()", prepare)
        self.assertLess(setup.index("ota_maintenance_active.store(ota_boot_validation_hold)"),
                        setup.index("storage_manager.begin()"))
        self.assertIn("!ota_manager.bootValidationHoldRequired()", loop)
        self.assertIn("ota_boot_validation_manager_ready &&", loop)
        self.assertLess(loop.index("ota_manager.update()"), loop.index("ota_maintenance_active.store(false)"))
        self.assertIn("ota_maintenance_active.load()", LITTLEFS)

    def test_rollback_is_latched_and_not_requested_in_a_loop(self):
        reject = OTA_CPP.split("bool OtaManager::rejectRunningImageAndRollback()", 1)[1].split(
            "bool OtaManager::confirmReadyToReboot", 1)[0]
        update = OTA_CPP.split("void OtaManager::update()", 1)[1].split(
            "bool OtaManager::setBootHealthHook", 1)[0]
        self.assertIn("if (rollback_attempted_) return false", reject)
        self.assertLess(reject.index("rollback_attempted_ = true"), reject.index("esp_ota_mark_app_invalid_rollback_and_reboot()"))
        self.assertIn("elapsed >= ROLLBACK_DEADLINE_MS && !rollback_attempted_", update)
        self.assertNotIn("while (", reject)

    def test_synthetic_storage_files_are_revalidated_without_recovery_or_migration(self):
        prepare = STORAGE.split("bool StorageManager::prepareFilesystemForBootConfirmation()", 1)[1].split(
            "bool StorageManager::isReady()", 1)[0]
        self.assertIn("validateCompleteMovementRecords()", prepare)
        self.assertIn("validateAccountSnapshots()", prepare)
        self.assertIn("ACCOUNT_TXN_FILE", prepare)
        self.assertNotIn("recoverPendingAccountMovement()", prepare)
        self.assertNotIn("writeFilesystemGuard(", prepare)
        self.assertNotIn("bank_littlefs.format(", prepare)

    def test_wifi_ota_stays_disabled_and_storage_format_remains_closed(self):
        self.assertIn("#define BANCO_OTA_ENABLED 0", CONFIG)
        storage_header = (ROOT / "src/storage/storage_manager.h").read_text(encoding="utf-8")
        self.assertIn("STORAGE_ALLOW_ONE_TIME_FORMAT = false", storage_header)
        self.assertIn(".format_if_mount_failed = false", LITTLEFS)


if __name__ == "__main__":
    unittest.main(verbosity=2)
