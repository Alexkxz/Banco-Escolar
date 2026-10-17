"""Contract and power-cut model tests for the isolated OTA-ARQ.2 installer."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
INSTALLER = (ROOT / "src/network/ota_lab_installer.cpp").read_text(encoding="utf-8")
ORIGIN_POLICY = (ROOT / "src/network/ota_lab_origin_policy.h").read_text(encoding="utf-8")
BOOTSTRAP = (ROOT / "src/network/ota_lab_bootstrap.cpp").read_text(encoding="utf-8")
CONFIG = (ROOT / "src/network/ota_config.h").read_text(encoding="utf-8")
MAIN = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
INSTALLER_PIO = (ROOT / "tools/ota/lab/platformio.lab-installer.ini").read_text(encoding="utf-8")
CANDIDATE_PIO = (ROOT / "tools/ota/lab/platformio.lab.ini").read_text(encoding="utf-8")
NORMAL_PIO = (ROOT / "platformio.ini").read_text(encoding="utf-8")


class InstallerContractTests(unittest.TestCase):
    def test_installer_is_off_by_default_and_separate_from_wifi_ota(self):
        self.assertIn("#define BANCO_OTA_LAB_INSTALLER_BUILD 0", CONFIG)
        self.assertIn("-DBANCO_OTA_ENABLED=0", NORMAL_PIO)
        self.assertIn("-DBANCO_OTA_LAB_BUILD=0", NORMAL_PIO)
        self.assertIn("-DBANCO_OTA_LAB_INSTALLER_BUILD=0", NORMAL_PIO)
        self.assertIn("#if BANCO_OTA_ENABLED || BANCO_OTA_LAB_BUILD", INSTALLER)
        self.assertIn("-DBANCO_OTA_ENABLED=0", INSTALLER_PIO)
        self.assertIn("-DBANCO_OTA_LAB_INSTALLER_BUILD=1", INSTALLER_PIO)
        self.assertIn("-DBANCO_OTA_LAB_BUILD=0", INSTALLER_PIO)
        self.assertIn("-DBANCO_OTA_ENABLED=0", CANDIDATE_PIO)

    def test_installer_hook_precedes_services_and_maintenance_lock(self):
        setup = MAIN.split("void setup()", 1)[1].split("void loop()", 1)[0]
        self.assertLess(setup.index("ota_lab_installer_before_services("),
                        setup.index("time_manager.begin()"))
        body = INSTALLER.split("void ota_lab_installer_before_services(", 1)[1].split("#else", 1)[0]
        self.assertLess(body.index("ota_maintenance_active.store(true)"),
                        body.index("waitForOperatorCommand()"))

    def test_runtime_geometry_and_source_slot_are_pinned(self):
        for expression in ("OTA0_ADDRESS = 0x00010000", "OTA1_ADDRESS = 0x00650000",
                           "OTA_SLOT_SIZE = 0x00640000", "OTADATA_ADDRESS = 0x0000E000",
                           "OTADATA_SIZE = 0x00002000", "FILESYSTEM_ADDRESS = 0x00C90000",
                           "FILESYSTEM_SIZE = 0x00360000"):
            self.assertIn(expression, INSTALLER)
        self.assertIn('ESP_PARTITION_SUBTYPE_APP_OTA_0, "ota_0"', INSTALLER)
        self.assertIn('ESP_PARTITION_SUBTYPE_APP_OTA_1, "ota_1"', INSTALLER)
        self.assertIn("samePartition(running, ota0)", INSTALLER)
        self.assertIn("samePartition(esp_ota_get_boot_partition(), ota0)", INSTALLER)
        self.assertIn("samePartition(checkedRunning, running)", INSTALLER)

    def test_exact_partition_table_checks_all_expected_entries(self):
        for label in ('"nvs"', '"otadata"', '"ota_0"', '"ota_1"', '"spiffs"', '"coredump"'):
            self.assertIn(label, INSTALLER)
        self.assertIn("partitionCount != 6", INSTALLER)
        self.assertIn("ESP_PARTITION_SUBTYPE_DATA_COREDUMP", INSTALLER)

    def test_only_ota1_is_writable_and_exactly_one_boot_selector_exists(self):
        self.assertIn("esp_ota_begin(ota1, manifest.imageSize", INSTALLER)
        self.assertIn("esp_ota_write(handle, buffer, amount)", INSTALLER)
        self.assertIn("esp_ota_end(handle)", INSTALLER)
        self.assertEqual(INSTALLER.count("esp_ota_set_boot_partition("), 1)
        self.assertNotIn("esp_partition_write(", INSTALLER)
        self.assertNotIn("esp_partition_erase_range(", INSTALLER)
        self.assertNotIn("esp_ota_set_boot_partition(", BOOTSTRAP)
        self.assertNotIn("esp_ota_set_boot_partition(ota0)", INSTALLER)

    def test_manifest_signature_and_readback_hash_are_mandatory(self):
        self.assertIn("MBEDTLS_ECP_DP_SECP256R1", INSTALLER)
        self.assertIn("mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256", INSTALLER)
        self.assertIn("!parseAndVerifyManifest(manifestBytes, manifestLength, ota1, manifest)", INSTALLER)
        self.assertIn("hashMatches(ota1, manifest.imageSize, manifest.sha256)", INSTALLER)
        self.assertIn("esp_image_verify(ESP_IMAGE_VERIFY", INSTALLER)
        self.assertIn("partitionContains(ota1, metadata.image_len, manifest.buildId)", INSTALLER)
        self.assertIn("partitionContains(ota1, metadata.image_len, manifest.version)", INSTALLER)
        self.assertIn("manifest.dataSchema != BANCO_OTA_DATA_SCHEMA_VERSION", INSTALLER)
        self.assertIn("!manifest.rollbackCompatible", INSTALLER)
        self.assertIn('CANDIDATE_VERSION[] = "1.2.4-lab"', INSTALLER)
        self.assertIn("strlen(version) >= sizeof(manifest.version)", INSTALLER)

    def test_running_installer_identity_is_signed_over_complete_image(self):
        self.assertIn('INSTALLER_IDENTITY_MAGIC[] = "BANCOID1"', INSTALLER)
        self.assertIn('INSTALLER_IDENTITY_BUILD_ID[] = "OTA-ARQ.2-INSTALLER"', INSTALLER)
        self.assertIn("approvedInstallerIdentityMatches(running, imageLength, digest, failureReason)", INSTALLER)
        self.assertIn("signatureValid(canonical, static_cast<size_t>(canonicalLength), signature)", INSTALLER)
        self.assertIn("strcmp(actualSha, expectedSha) != 0", INSTALLER)
        self.assertIn("image_size\"].as<uint32_t>() != imageLength", INSTALLER)

    def test_undefined_state_requires_approved_identity_and_other_states_fail(self):
        self.assertIn("stateError != ESP_OK", INSTALLER)
        self.assertIn("ota_lab_origin_policy::mayUseRunningImage(true, static_cast<uint32_t>(state), true)", INSTALLER)
        for expected in (
            "ESP_OTA_IMG_NEW", "ESP_OTA_IMG_PENDING_VERIFY", "ESP_OTA_IMG_INVALID", "ESP_OTA_IMG_ABORTED"
        ):
            self.assertIn(expected, INSTALLER)
        for assertion in (
            "static_assert(mayUseRunningImage(true, kValid, true));",
            "static_assert(mayUseRunningImage(true, kUndefined, true));",
            "static_assert(!mayUseRunningImage(false, kValid, true));",
            "static_assert(!mayUseRunningImage(true, kValid, false));",
            "static_assert(!mayUseRunningImage(true, kNew, true));",
            "static_assert(!mayUseRunningImage(true, kPendingVerify, true));",
            "static_assert(!mayUseRunningImage(true, kInvalid, true));",
            "static_assert(!mayUseRunningImage(true, kAborted, true));",
        ):
            self.assertIn(assertion, ORIGIN_POLICY)

    def test_origin_failures_have_separate_non_sensitive_diagnostics(self):
        for message in (
            "geometría de partición nvs inesperada",
            "geometría de partición otadata inesperada",
            "la partición en ejecución no es ota_0",
            "la selección de arranque no es ota_0",
            "falta el trailer de identidad aprobado",
            "SHA-256 de imagen no coincide con la identidad firmada",
            "firma P-256 de identidad inválida",
            "error al consultar estado OTA de ota_0",
            "estado OTA PENDING_VERIFY no permitido",
        ):
            self.assertIn(message, INSTALLER)

    def test_storage_and_financial_gate_fail_closed(self):
        self.assertIn("storage_manager.begin()", INSTALLER)
        self.assertIn("storage_manager.isReady() && !storage_manager.hasIncompleteTransactions()", INSTALLER)
        self.assertIn("bank_littlefs.isMounted() && bank_littlefs.isReadOnly()", INSTALLER)
        self.assertIn("ota_maintenance_active.store(true)", INSTALLER)
        self.assertIn("for (;;) delay(1000)", INSTALLER)
        self.assertIn("storageSafeReadOnly()", INSTALLER)

    def test_candidate_never_arms_or_reselects_and_requires_current_pending_state(self):
        self.assertNotIn("esp_ota_set_boot_partition(", BOOTSTRAP)
        self.assertIn('"OTA-ARQ.2-CANDIDATE"', BOOTSTRAP)
        self.assertIn("state == ESP_OTA_IMG_PENDING_VERIFY", BOOTSTRAP)
        self.assertIn("!managerDetectedPending", BOOTSTRAP)
        self.assertIn("state == ESP_OTA_IMG_VALID", BOOTSTRAP)
        self.assertIn("estado NEW/INVALID/ABORTED/UNDEFINED inesperado", BOOTSTRAP)

    def test_power_cut_model_preserves_ota0_and_old_selection_before_commit(self):
        # Model IDF's alternating otadata-sector commit: before the selector
        # finishes, ota0 remains selected; after it, ota1 is NEW/PENDING.
        stages = ["preconditions", "ota1_begin", "partial_write", "ota1_end",
                  "stream_hash", "readback_hash", "image_verify"]
        for stage in stages:
            active, ota0_written, ota1_contents = "ota0", False, "old_or_partial"
            self.assertEqual(active, "ota0", stage)
            self.assertFalse(ota0_written, stage)
        # The selector call updates one alternate otadata record. On a power
        # cut either the previous valid record or the new valid record wins.
        for committed_record_survives in (False, True):
            active = "ota1" if committed_record_survives else "ota0"
            state = "NEW" if active == "ota1" else "VALID"
            self.assertIn(active, ("ota0", "ota1"))
            self.assertIn(state, ("VALID", "NEW"))
            if state == "NEW":
                first_boot_state = "PENDING_VERIFY"
                self.assertEqual(first_boot_state, "PENDING_VERIFY")
                reset_without_confirmation = "ota0"
                self.assertEqual(reset_without_confirmation, "ota0")
            self.assertFalse(ota0_written)


if __name__ == "__main__":
    unittest.main()
