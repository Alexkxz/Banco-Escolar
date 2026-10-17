"""Host-side source-contract tests for pending-image LittleFS protection."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
STORAGE_FS_H = (ROOT / "src/storage/bank_littlefs.h").read_text(encoding="utf-8")
STORAGE_FS_CPP = (ROOT / "src/storage/bank_littlefs.cpp").read_text(encoding="utf-8")
STORAGE_CPP = (ROOT / "src/storage/storage_manager.cpp").read_text(encoding="utf-8")
STORAGE_H = (ROOT / "src/storage/storage_manager.h").read_text(encoding="utf-8")
MAIN = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
CONFIG = (ROOT / "src/network/ota_config.h").read_text(encoding="utf-8")


class LittleFsValidationContractTests(unittest.TestCase):
    def test_pending_state_is_set_before_storage_mount(self):
        setup = MAIN.split("void setup()", 1)[1].split("void loop()", 1)[0]
        self.assertLess(setup.index("ota_maintenance_active.store(ota_boot_validation_hold)"),
                        setup.index("storage_manager.begin()"))

    def test_pending_mount_is_read_only_and_normal_mount_retains_growth(self):
        mount = STORAGE_CPP.split("bool mountOrInitializeFilesystem()", 1)[1].split(
            "bool findStorageTestRecord", 1)[0]
        self.assertIn("bank_littlefs.mount(validating_ota_image", mount)
        self.assertIn(".read_only = read_only", STORAGE_FS_CPP)
        self.assertIn(".grow_on_mount = grow_on_mount", STORAGE_FS_CPP)
        self.assertIn("!validating_ota_image", mount)

    def test_mount_never_formats_automatically_or_on_pending_failure(self):
        self.assertIn(".format_if_mount_failed = false", STORAGE_FS_CPP)
        mount = STORAGE_CPP.split("bool mountOrInitializeFilesystem()", 1)[1].split(
            "bool findStorageTestRecord", 1)[0]
        self.assertLess(mount.index("if (validating_ota_image)"), mount.index("bank_littlefs.format()"))
        self.assertIn("STORAGE_ALLOW_ONE_TIME_FORMAT = false", STORAGE_H)

    def test_storage_records_are_validated_read_only_before_health_passes(self):
        begin = STORAGE_CPP.split("bool StorageManager::begin()", 1)[1].split(
            "bool StorageManager::activateNormalFilesystem()", 1)[0]
        self.assertIn("!validateCompleteMovementRecords()", begin)
        self.assertIn("!validateAccountSnapshots()", begin)
        self.assertIn("bank_littlefs.pathStatus(ACCOUNT_TXN_FILE)", begin)
        self.assertIn("if (!validating_ota_image && !recoverPendingAccountMovement())", begin)

    def test_pending_financial_journal_rejects_candidate_without_recovery_writes(self):
        begin = STORAGE_CPP.split("bool StorageManager::begin()", 1)[1].split(
            "bool StorageManager::activateNormalFilesystem()", 1)[0]
        rejected = begin.split("if (validating_ota_image &&", 1)[1].split("    if (!validating_ota_image", 1)[0]
        self.assertIn("pending_transaction_status != BankLittleFsPathStatus::MISSING", rejected)
        self.assertNotIn("recoverPendingAccountMovement()", rejected)

    def test_rw_preparation_occurs_before_confirmation_without_growth_or_journal_recovery(self):
        preparation = STORAGE_CPP.split("bool StorageManager::prepareFilesystemForBootConfirmation()", 1)[1].split(
            "bool StorageManager::isReady()", 1)[0]
        begin = STORAGE_CPP.split("bool StorageManager::begin()", 1)[1].split(
            "bool StorageManager::prepareFilesystemForBootConfirmation()", 1)[0]
        loop = MAIN.split("void loop()", 1)[1]
        update = (ROOT / "src/network/ota_manager.cpp").read_text(encoding="utf-8").split(
            "void OtaManager::update()", 1)[1].split("bool OtaManager::setBootHealthHook", 1)[0]
        self.assertIn("bank_littlefs.remountReadWriteForBootValidation()", preparation)
        self.assertIn("totalBytes() == EXPECTED_FS_SIZE", preparation)
        self.assertLess(update.index("boot_storage_prepare_hook_()"), update.index("confirmRunningImageHealthy()"))
        self.assertIn("!validating_ota_image && !recoverPendingAccountMovement()", begin)
        self.assertNotIn("recoverPendingAccountMovement()", preparation)
        self.assertIn("!ota_manager.bootValidationHoldRequired()", loop)
        self.assertNotIn("prepareFilesystemForBootConfirmation", loop)

    def test_preconfirmation_mount_contract_is_version_pinned_and_no_grow(self):
        self.assertIn("ESP_LITTLEFS_VERSION_MAJOR == 1", STORAGE_FS_CPP)
        self.assertIn("ESP_LITTLEFS_VERSION_MINOR == 16", STORAGE_FS_CPP)
        self.assertIn("ESP_LITTLEFS_VERSION_PATCH == 1", STORAGE_FS_CPP)
        probe = STORAGE_FS_CPP.split(
            "bool BankLittleFS::remountReadWriteForBootValidation()", 1)[1].split(
                "bool BankLittleFS::isMounted()", 1)[0]
        self.assertIn("mountInternal(false, base_path_, partition_label_, false, true)", probe)
        self.assertNotIn("mountInternal(false, base_path_, partition_label_, true,", probe)
        self.assertIn("format_if_mount_failed = false", STORAGE_FS_CPP)

    def test_generic_writable_mount_is_denied_under_ota_maintenance(self):
        mount = STORAGE_FS_CPP.split("bool BankLittleFS::mountInternal(", 1)[1].split(
            "bool BankLittleFS::unmount()", 1)[0]
        self.assertIn("ota_maintenance_active.load() && !read_only && !boot_validation_probe", mount)
        self.assertIn("boot_validation_probe &&\n         (!ota_maintenance_active.load() || read_only || grow_on_mount)", mount)
        self.assertIn("remountReadWriteForBootValidation", STORAGE_FS_H)

    def test_read_only_boot_validation_checks_storage_without_mutation_helpers(self):
        begin = STORAGE_CPP.split("bool StorageManager::begin()", 1)[1].split(
            "bool StorageManager::prepareFilesystemForBootConfirmation()", 1)[0]
        validation = begin.split("if (validating_ota_image &&", 1)[1].split(
            "    if (!validating_ota_image", 1)[0]
        for forbidden in ("format()", "mkdir(", "open(", "recoverPendingAccountMovement()",
                          "writeFilesystemGuard("):
            self.assertNotIn(forbidden, validation)
        self.assertIn("validateCompleteMovementRecords()", validation)
        self.assertIn("validateAccountSnapshots()", validation)

    def test_incomplete_or_unread_tail_rejects_candidate_without_repair(self):
        account_validation = STORAGE_CPP.split("bool validateAccountSnapshots()", 1)[1].split(
            "bool validateCompleteMovementRecords()", 1)[0]
        movement_validation = STORAGE_CPP.split("bool validateCompleteMovementRecords()", 1)[1].split(
            "bool writePendingTransaction", 1)[0]
        for validation in (account_validation, movement_validation):
            self.assertIn("file.position() == file.size()", validation)
            self.assertIn("return reached_file_end && length == 0", validation)
            self.assertNotIn("file.write(", validation)
            self.assertNotIn("bank_littlefs.remove(", validation)

    def test_unmount_and_rw_remount_failures_fail_closed(self):
        remount = STORAGE_FS_CPP.split(
            "bool BankLittleFS::remountReadWriteForBootValidation()", 1)[1].split(
                "bool BankLittleFS::isMounted()", 1)[0]
        prepare = STORAGE_CPP.split(
            "bool StorageManager::prepareFilesystemForBootConfirmation()", 1)[1].split(
                "bool StorageManager::isReady()", 1)[0]
        self.assertIn("if (!unmount()) return false", remount)
        self.assertIn("mountInternal(true, base_path_, partition_label_, false, false)", remount)
        self.assertIn("state_ = StorageState::STORAGE_ERROR", prepare)
        self.assertIn("recovery_required_ = true", prepare)
        self.assertIn("return false", prepare)

    def test_failed_rw_preparation_requests_rollback_without_releasing_gate(self):
        update = (ROOT / "src/network/ota_manager.cpp").read_text(encoding="utf-8").split(
            "void OtaManager::update()", 1)[1].split("bool OtaManager::setBootHealthHook", 1)[0]
        failure = update.split("if (boot_storage_prepare_hook_ == nullptr || !boot_storage_prepare_hook_())", 1)[1].split(
            "// Keep the school-data gate", 1)[0]
        loop = MAIN.split("void loop()", 1)[1]
        self.assertIn("rejectRunningImageAndRollback()", failure)
        self.assertNotIn("confirmRunningImageHealthy()", failure)
        self.assertIn("!ota_manager.bootValidationHoldRequired()", loop)
        self.assertIn("ota_maintenance_active.store(false)", loop)

    def test_normal_mount_retains_existing_growth_policy_and_wifi_ota_stays_disabled(self):
        self.assertIn(".grow_on_mount = grow_on_mount", STORAGE_FS_CPP)
        self.assertIn("!validating_ota_image", STORAGE_CPP)
        self.assertIn("#define BANCO_OTA_ENABLED 0", CONFIG)

    def test_vfs_denies_file_and_directory_writes_during_transition(self):
        self.assertIn('strpbrk(mode, "wa+")', STORAGE_FS_CPP)
        for operation in ("rename", "remove", "mkdir", "rmdir"):
            self.assertIn(f"bool {operation}(const char *", STORAGE_FS_CPP)
        self.assertGreaterEqual(STORAGE_FS_CPP.count("ota_maintenance_active.load()"), 5)

    def test_optional_data_files_distinguish_missing_from_io_errors(self):
        self.assertIn("errno == ENOENT ? BankLittleFsPathStatus::MISSING", STORAGE_FS_CPP)
        self.assertIn("account_file != BankLittleFsPathStatus::PRESENT) return false", STORAGE_CPP)
        activity = (ROOT / "src/storage/activity_storage_manager.cpp").read_text(encoding="utf-8")
        claims = (ROOT / "src/storage/activity_claim_storage_manager.cpp").read_text(encoding="utf-8")
        self.assertIn("file_status != BankLittleFsPathStatus::PRESENT", activity)
        self.assertIn("file_status != BankLittleFsPathStatus::PRESENT", claims)


if __name__ == "__main__":
    unittest.main(verbosity=2)
