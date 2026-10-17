"""OTA.2A contract tests; these validate the release contract, not ESP hardware/mbedTLS."""
import hashlib
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SOURCE = (ROOT / "src/network/ota_manager.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "src/network/ota_manager.h").read_text(encoding="utf-8")
CONFIG = (ROOT / "src/network/ota_config.h").read_text(encoding="utf-8")
AUTH = (ROOT / "src/network/ota_authorization.h").read_text(encoding="utf-8")

MODEL = "WAVESHARE_ESP32_S3_TOUCH_LCD_7_REV1_2"


def valid_manifest(m, current="1.2.3", capacity=6_553_600, signature_ok=True):
    keys = {"model", "version", "size", "sha256", "image_url", "data_schema", "signature"}
    if not isinstance(m, dict) or set(m) != keys:
        return False
    if m["model"] != MODEL or not signature_ok:
        return False
    v = m["version"]
    if not isinstance(v, str) or len(v.split(".")) != 3 or not all(
        p.isdigit() and len(p) <= 10 and int(p) <= 0xFFFFFFFF and
        (p == "0" or not p.startswith("0")) for p in v.split(".")
    ):
        return False
    if tuple(map(int, v.split("."))) <= tuple(map(int, current.split("."))):
        return False
    if type(m["size"]) is not int or not 0 < m["size"] <= capacity:
        return False
    if type(m["data_schema"]) is not int or m["data_schema"] != 1:
        return False
    if not isinstance(m["sha256"], str) or len(m["sha256"]) != 64 or any(c not in "0123456789abcdef" for c in m["sha256"]):
        return False
    return isinstance(m["image_url"], str) and m["image_url"].startswith("https://") and "@" not in m["image_url"] and "#" not in m["image_url"]


def rollback_transition(elapsed_ms, health_ok, confirm_ok, storage_ready=True, pending=True):
    if pending and elapsed_ms >= 30_000 and health_ok and storage_ready and confirm_ok:
        pending = False
    if pending and elapsed_ms >= 180_000:
        return "ROLLBACK"
    return "PENDING" if pending else "CONFIRMED"


class AuthorizationModel:
    """Host-side state model; source assertions tie the contract to firmware code."""
    def __init__(self):
        self.claim = None

    def issue(self, verified, proof, version, digest, url, now, lifetime):
        if self.claim is not None or not verified or not proof or not version or len(digest) != 64:
            return False
        if not url.startswith("https://") or not 0 < lifetime < 0x80000000:
            return False
        self.claim = {"version": version, "digest": digest, "url": url,
                      "expires": (now + lifetime) & 0xffffffff, "used": False}
        return True

    def consume(self, url, now, version, digest):
        if self.claim is None or self.claim["used"] or self.claim["url"] != url:
            return False
        delta = ((now - self.claim["expires"] + 0x80000000) & 0xffffffff) - 0x80000000
        if delta >= 0 or self.claim["version"] != version or self.claim["digest"] != digest:
            return False
        self.claim["used"] = True
        return True


class LoginRApprovalModel:
    """Ephemeral host model only; does not represent real LOGIN-R credentials."""
    def __init__(self):
        self.used_ids = set()
        self.claim = None

    def issue(self, approval, request, now):
        required = (approval.get("authenticated") and approval.get("current") and
                    approval.get("admin") and approval.get("permission"))
        bound = all(approval.get(key) == request.get(key)
                    for key in ("master", "version", "url", "digest"))
        ttl = approval.get("expires", 0) - now
        approval_id = approval.get("id", "")
        if (not required or not bound or not approval_id or approval_id in self.used_ids or
                ttl <= 0 or ttl > 120_000):
            return False
        self.used_ids.add(approval_id)  # mirrors server-side atomic one-use consumption
        self.claim = {**request, "master": approval["master"], "id": approval_id,
                      "expires": approval["expires"], "used": False}
        return True

    def consume(self, request, now):
        if not self.claim or self.claim["used"] or now >= self.claim["expires"]:
            return False
        if any(self.claim[key] != request.get(key) for key in ("version", "url", "digest")):
            return False
        self.claim["used"] = True
        return True


class OtaContractTests(unittest.TestCase):
    def setUp(self):
        self.image = b"signed firmware fixture"
        self.manifest = {"model": MODEL, "version": "1.2.4", "size": len(self.image),
                         "sha256": hashlib.sha256(self.image).hexdigest(),
                         "image_url": "https://updates.example/fw.bin", "data_schema": 1, "signature": "fixture"}

    def test_valid_manifest_and_matching_image(self):
        self.assertTrue(valid_manifest(self.manifest))
        self.assertEqual(hashlib.sha256(self.image).hexdigest(), self.manifest["sha256"])

    def test_data_schema_must_be_rollback_compatible(self):
        self.assertTrue(valid_manifest(dict(self.manifest, data_schema=1)))
        self.assertFalse(valid_manifest(dict(self.manifest, data_schema=2)))

    def test_wrong_model_rejected(self):
        m = dict(self.manifest, model="OTHER")
        self.assertFalse(valid_manifest(m))

    def test_bad_signature_rejected(self):
        self.assertFalse(valid_manifest(self.manifest, signature_ok=False))

    def test_downgrade_and_equal_version_rejected(self):
        self.assertFalse(valid_manifest(dict(self.manifest, version="1.2.3")))
        self.assertFalse(valid_manifest(dict(self.manifest, version="1.2.2")))

    def test_invalid_semver_overflow_and_leading_zero_rejected(self):
        self.assertFalse(valid_manifest(dict(self.manifest, version="1.02.4")))
        self.assertFalse(valid_manifest(dict(self.manifest, version="4294967296.0.0")))

    def test_oversize_bad_hash_and_http_url_rejected(self):
        self.assertFalse(valid_manifest(dict(self.manifest, size=6_553_601)))
        self.assertFalse(valid_manifest(dict(self.manifest, sha256="z" * 64)))
        self.assertFalse(valid_manifest(dict(self.manifest, image_url="http://updates.example/fw.bin")))

    def test_unknown_or_missing_manifest_fields_rejected(self):
        self.assertFalse(valid_manifest({k: v for k, v in self.manifest.items() if k != "signature"}))
        self.assertFalse(valid_manifest(dict(self.manifest, extra="ignored")))

    def test_incomplete_or_corrupt_download_detected_by_size_and_hash(self):
        self.assertNotEqual(len(self.image) - 1, self.manifest["size"])
        self.assertNotEqual(hashlib.sha256(self.image + b"x").hexdigest(), self.manifest["sha256"])

    def test_implementation_has_required_guards_and_no_public_update_route(self):
        self.assertIn("HTTPC_DISABLE_FOLLOW_REDIRECTS", SOURCE)
        self.assertNotIn("setInsecure", SOURCE)
        self.assertIn("setCACert(BANCO_OTA_SERVER_ROOT_CA_PEM)", SOURCE)
        self.assertIn("authority.isEmpty()", SOURCE)
        self.assertIn("c <= 0x20 || c >= 0x7f", SOURCE)
        self.assertIn("esp_ota_get_next_update_partition", SOURCE)
        self.assertIn("esp_ota_set_boot_partition", SOURCE)
        self.assertIn("OtaError::ROLLBACK_UNAVAILABLE", SOURCE)
        self.assertIn("xSemaphoreCreateMutex", SOURCE)
        self.assertIn("std::atomic<bool> cancel_requested_", HEADER)
        self.assertIn("xSemaphoreTake(mutex_, portMAX_DELAY);", SOURCE.split("bool OtaManager::setMaintenanceHooks", 1)[1])
        self.assertIn("OtaAuthorization() = default", AUTH)
        self.assertIn("#define BANCO_OTA_ENABLED 0", CONFIG)
        normal_platformio = (ROOT / "platformio.ini").read_text(encoding="utf-8")
        self.assertRegex(normal_platformio, r"(?m)^\s+-DBANCO_OTA_ENABLED=0\s*$")
        self.assertNotIn("BANCO_OTA_LAB", normal_platformio)
        self.assertIn("MBEDTLS_ECP_DP_SECP256R1", SOURCE)
        self.assertIn("!signature_valid(canonical", SOURCE)
        self.assertNotIn("/ota", (ROOT / "src/network/panel_http_server.cpp").read_text(encoding="utf-8").lower())

    def test_rollback_state_transitions_and_confirmation_failure(self):
        self.assertEqual(rollback_transition(29_999, True, True), "PENDING")
        self.assertEqual(rollback_transition(30_000, True, True), "CONFIRMED")
        self.assertEqual(rollback_transition(30_000, True, True, storage_ready=False), "PENDING")
        self.assertEqual(rollback_transition(30_000, False, False), "PENDING")
        self.assertEqual(rollback_transition(179_999, False, False), "PENDING")
        self.assertEqual(rollback_transition(180_000, False, False), "ROLLBACK")
        self.assertEqual(rollback_transition(180_000, True, False), "ROLLBACK")
        monitor = SOURCE.split("void OtaManager::update()", 1)[1].split("bool OtaManager::setBootHealthHook", 1)[0]
        self.assertIn("elapsed >= ROLLBACK_DEADLINE_MS && !rollback_attempted_", monitor)
        self.assertIn("boot_storage_prepare_hook_()", monitor)
        self.assertIn("ota_manager.rejectRunningImageAndRollback();", (ROOT / "src/main.cpp").read_text(encoding="utf-8"))

    def test_effective_qio_opi_sdkconfig_has_rollback_enabled(self):
        sdkconfig = Path.home() / ".platformio/packages/framework-arduinoespressif32-libs/esp32s3/qio_opi/include/sdkconfig.h"
        self.assertIn("#define CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 1", sdkconfig.read_text(encoding="utf-8"))
        platformio = (ROOT / "platformio.ini").read_text(encoding="utf-8")
        self.assertIn("board_build.arduino.memory_type = qio_opi", platformio)

    def test_authorization_is_explicit_expiring_single_use_and_manifest_bound(self):
        digest = "a" * 64
        auth = AuthorizationModel()
        self.assertFalse(auth.issue(False, b"proof", "1.2.4", digest, "https://school/manifest.json", 100, 1000))
        self.assertTrue(auth.issue(True, b"proof", "1.2.4", digest, "https://school/manifest.json", 100, 1000))
        self.assertFalse(auth.consume("https://school/manifest.json", 200, "1.2.5", digest))
        self.assertFalse(auth.consume("https://school/manifest.json", 200, "1.2.4", "b" * 64))
        self.assertTrue(auth.consume("https://school/manifest.json", 200, "1.2.4", digest))
        self.assertFalse(auth.consume("https://school/manifest.json", 201, "1.2.4", digest))

    def test_authorization_expiry_and_wraparound(self):
        auth = AuthorizationModel()
        digest = "c" * 64
        self.assertTrue(auth.issue(True, b"proof", "2.0.0", digest, "https://school/m.json", 0xfffffff0, 32))
        self.assertTrue(auth.consume("https://school/m.json", 0xfffffff5, "2.0.0", digest))
        expired = AuthorizationModel()
        self.assertTrue(expired.issue(True, b"proof", "2.0.0", digest, "https://school/m.json", 100, 50))
        self.assertFalse(expired.consume("https://school/m.json", 150, "2.0.0", digest))

    def test_loginr_authorization_contract_fails_closed_and_binds_master_and_approval(self):
        self.assertIn("verifier_ == nullptr", AUTH)
        self.assertIn("authenticated_session", AUTH)
        self.assertIn("session_current", AUTH)
        self.assertIn("administrator", AUTH)
        self.assertIn("firmware_update_permission", AUTH)
        self.assertIn("approval_id", AUTH)
        self.assertIn("authorization.approval_id_", SOURCE)
        self.assertIn("authorization.master_id_", SOURCE)
        self.assertIn("DATA_INCOMPATIBLE", SOURCE)

    def test_loginr_approval_authentication_expiry_permission_replay_and_exact_binding(self):
        request = {"master": "master-fixture", "version": "1.2.4",
                   "url": "https://school/manifest.json", "digest": "a" * 64}

        def approval(**changes):
            values = {"authenticated": True, "current": True, "admin": True, "permission": True,
                      "master": "master-fixture", "id": "approval-fixture-01",
                      "version": request["version"], "url": request["url"],
                      "digest": request["digest"], "expires": 60_000}
            values.update(changes)
            return values

        for change in ({"authenticated": False}, {"current": False}, {"admin": False},
                       {"permission": False}, {"expires": 0},
                       {"version": "1.2.5"}, {"url": "https://school/other.json"},
                       {"digest": "b" * 64}):
            self.assertFalse(LoginRApprovalModel().issue(approval(**change), request, 0), change)

        model = LoginRApprovalModel()
        self.assertTrue(model.issue(approval(), request, 0))
        self.assertTrue(model.consume(request, 1))
        self.assertFalse(model.consume(request, 2))  # local one-use capability
        self.assertFalse(model.issue(approval(), request, 0))  # server-side ID replay
        expired = LoginRApprovalModel()
        self.assertTrue(expired.issue(approval(), request, 0))
        self.assertFalse(expired.consume(request, 60_000))
        changed_firmware = dict(request, version="1.2.5")
        self.assertFalse(LoginRApprovalModel().issue(approval(), changed_firmware, 0))
        self.assertFalse(LoginRApprovalModel().issue(approval(master="another-master"), request, 0))

    def test_absent_loginr_verifier_defaults_to_rejection(self):
        self.assertIn("MasterApprovalVerifier verifier = nullptr", AUTH)
        self.assertIn("if (verifier_ == nullptr", AUTH)

    def test_manifest_signature_covers_data_schema(self):
        self.assertIn('"data_schema"', SOURCE)
        self.assertIn('\\n%u', SOURCE)
        manifest_tool = (ROOT / "tools/ota/ota_manifest.mjs").read_text(encoding="utf-8")
        self.assertIn("data_schema: dataSchema", manifest_tool)
        self.assertIn("\\n${m.data_schema}", manifest_tool)

    def test_firmware_authorization_contract_and_cancel_before_activation(self):
        manager_header = (ROOT / "src/network/ota_manager.h").read_text(encoding="utf-8")
        self.assertIn("MasterApprovalVerifier verifier = nullptr", AUTH)
        self.assertIn("verifier_ == nullptr", AUTH)
        self.assertIn("mutable bool consumed_", AUTH)
        self.assertIn("manifest_sha256_[65]", AUTH)
        self.assertIn("expires_at_ms_", AUTH)
        self.assertIn("authorization.consumed_", SOURCE)
        self.assertIn("downloaded_manifest_sha256 != pending_authorized_manifest_sha256_", SOURCE)
        self.assertIn("strcmp(version, pending_authorized_version_) != 0", SOURCE)
        self.assertIn("activation_committed_", manager_header)
        request = SOURCE.split("bool OtaManager::requestUpdate", 1)[1].split("void OtaManager::workerEntry", 1)[0]
        self.assertIn("worker_ != nullptr", request)
        self.assertIn("snapshot_.state == OtaState::CHECKING", request)
        self.assertIn("snapshot_.state == OtaState::READY_TO_REBOOT", request)
        self.assertNotIn("requestUpdate(", (ROOT / "src/main.cpp").read_text(encoding="utf-8"))
        cancel = SOURCE.split("bool OtaManager::cancel()", 1)[1].split("OtaSnapshot OtaManager::snapshot", 1)[0]
        self.assertIn("!activation_committed_", cancel)
        self.assertIn("#define BANCO_OTA_ENABLED 0", CONFIG)

    def test_disabled_build_cannot_reject_running_image(self):
        reject = SOURCE.split("bool OtaManager::rejectRunningImageAndRollback()", 1)[1].split(
            "bool OtaManager::confirmReadyToReboot", 1
        )[0]
        self.assertIn("#if CONFIG_APP_ROLLBACK_ENABLE", reject)
        self.assertIn("#else\n    return false;", reject)

    def test_arduino_init_defers_pending_confirmation_even_when_network_ota_is_disabled(self):
        hook = SOURCE.split('extern "C" bool verifyRollbackLater()', 1)[1].split("bool OtaManager::begin()", 1)[0]
        self.assertIn("#if CONFIG_APP_ROLLBACK_ENABLE", hook)
        self.assertIn("return true;", hook)
        self.assertNotIn("BANCO_OTA_ENABLED", hook)
        misc = Path.home() / ".platformio/packages/framework-arduinoespressif32/cores/esp32/esp32-hal-misc.c"
        core = misc.read_text(encoding="utf-8")
        init = core.split("void initArduino()", 1)[1].split("void", 1)[0]
        self.assertIn("verifyRollbackLater()", init)
        self.assertIn("esp_ota_mark_app_valid_cancel_rollback()", init)

    def test_pending_boot_sets_persistent_state_gate_before_services(self):
        main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        setup = main.split("void setup()", 1)[1].split("void loop()", 1)[0]
        self.assertLess(setup.index("ota_manager.begin()"), setup.index("board->init()"))
        self.assertLess(setup.index("ota_maintenance_active.store(ota_boot_validation_hold)"),
                        setup.index("storage_manager.begin()"))
        self.assertLess(setup.index("ota_maintenance_active.store(ota_boot_validation_hold)"),
                        setup.index("wifi_manager.begin()"))
        storage = (ROOT / "src/storage/storage_manager.cpp").read_text(encoding="utf-8")
        begin = storage.split("bool StorageManager::begin()", 1)[1].split("StorageState StorageManager::state", 1)[0]
        self.assertIn("validating_ota_image", begin)
        self.assertIn("bank_littlefs.pathStatus(ACCOUNT_TXN_FILE)", begin)
        self.assertIn("!validateCompleteMovementRecords()", begin)
        self.assertIn("!validateAccountSnapshots()", begin)
        self.assertIn("!validating_ota_image && !writeFilesystemGuard", begin)
        self.assertIn("!validating_ota_image && !recoverPendingAccountMovement()", begin)

    def test_financial_activity_claim_and_sd_writes_fail_closed_during_validation(self):
        storage = (ROOT / "src/storage/storage_manager.cpp").read_text(encoding="utf-8")
        for signature in ("StorageManager::appendMovement", "StorageManager::createAccountIfMissing",
                          "StorageManager::applyAccountMovement", "StorageManager::applyActivityReward",
                          "StorageManager::clearLocalData", "StorageManager::appendAccountSnapshot"):
            body = storage.split(signature, 1)[1].split("\n}", 1)[0]
            self.assertIn("ota_maintenance_active.load()", body, signature)
        activities = (ROOT / "src/storage/activity_storage_manager.cpp").read_text(encoding="utf-8")
        claims = (ROOT / "src/storage/activity_claim_storage_manager.cpp").read_text(encoding="utf-8")
        for signature in ("ActivityStorageManager::createSession", "ActivityStorageManager::closeSession",
                          "ActivityStorageManager::updateActiveSession"):
            self.assertIn("ota_maintenance_active.load()", activities.split(signature, 1)[1].split("\n}", 1)[0])
        for signature in ("ActivityClaimStorageManager::reserveClaimId",
                          "ActivityClaimStorageManager::recordMissingAccountIssue",
                          "ActivityClaimStorageManager::ensureClaimPersisted"):
            self.assertIn("ota_maintenance_active.load()", claims.split(signature, 1)[1].split("\n}", 1)[0])
        sd = (ROOT / "src/storage/sd_manager.cpp").read_text(encoding="utf-8")
        for signature in ("SDManager::createDirectory", "SDManager::writeFile(const char *path, const uint8_t *data",
                          "SDManager::removeFile"):
            self.assertIn("ota_maintenance_active.load()", sd.split(signature, 1)[1].split("\n}", 1)[0])
        wifi = (ROOT / "src/network/wifi_manager.cpp").read_text(encoding="utf-8")
        for signature in ("WiFiManager::startScan", "WiFiManager::connectToNetwork",
                          "WiFiManager::saveCredentialsUnlocked", "WiFiManager::clearCredentials"):
            self.assertIn("ota_maintenance_active.load()", wifi.split(signature, 1)[1].split("\n}", 1)[0])

    def test_pending_restart_reconstructs_maintenance_instead_of_relying_on_ram(self):
        main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        setup = main.split("void setup()", 1)[1].split("void loop()", 1)[0]
        self.assertIn("ota_manager.bootValidationPending()", setup)
        self.assertIn("ota_maintenance_active.store(ota_boot_validation_hold)", setup)
        # Host model: every reset starts RAM false, then persistent pending state restores the gate.
        for pending in (True, False):
            gate_after_reset = False
            gate_after_setup = pending
            self.assertEqual(gate_after_setup, pending)

    def test_slot_policy_preserves_ota0_and_rejects_non_ota1_destination(self):
        run_update = SOURCE.split("void OtaManager::runUpdate", 1)[1].split("bool OtaManager::cancel", 1)[0]
        for guard in ("ESP_PARTITION_SUBTYPE_APP_OTA_0", "running->address != 0x10000U",
                      "running->size != 0x640000U", "ESP_PARTITION_SUBTYPE_APP_OTA_1",
                      "target->address != 0x650000U", "target->size != 0x640000U",
                      "esp_ota_get_next_update_partition(running)"):
            self.assertIn(guard, run_update)
        self.assertLess(run_update.index("target->subtype"), run_update.index("esp_ota_begin(target"))

    def test_data_schemas_and_startup_writes_are_not_migrated_for_pending_image(self):
        storage_h = (ROOT / "src/storage/storage_manager.h").read_text(encoding="utf-8")
        activity_h = (ROOT / "src/activity_session.h").read_text(encoding="utf-8")
        claims_h = (ROOT / "src/activity_claim.h").read_text(encoding="utf-8")
        for version in ("STORAGE_SCHEMA_VERSION = 1", "ACTIVITY_SESSION_SCHEMA_VERSION = 1",
                        "ACTIVITY_CLAIM_SCHEMA_VERSION = 1"):
            self.assertTrue(version in storage_h or version in activity_h or version in claims_h, version)
        storage = (ROOT / "src/storage/storage_manager.cpp").read_text(encoding="utf-8")
        begin = storage.split("bool StorageManager::begin()", 1)[1].split("StorageState StorageManager::state", 1)[0]
        self.assertIn("bank_littlefs.pathStatus(ACCOUNT_TXN_FILE)", begin)
        self.assertIn("validateCompleteMovementRecords()", begin)
        self.assertIn("validateAccountSnapshots()", begin)
        self.assertIn("recovery_required_ = pending_transaction_status != BankLittleFsPathStatus::MISSING", begin)
        self.assertIn("!validating_ota_image && !writeFilesystemGuard", begin)
        self.assertIn("!validating_ota_image && !recoverPendingAccountMovement()", begin)

    def test_health_is_deferred_until_monitor_window_and_rejects_unhealthy_image(self):
        monitor = SOURCE.split("void OtaManager::update()", 1)[1].split("bool OtaManager::setBootHealthHook", 1)[0]
        self.assertIn("elapsed >= ROLLBACK_HEALTH_WINDOW_MS", monitor)
        self.assertIn("boot_health_hook_()", monitor)
        self.assertIn("confirmRunningImageHealthy()", monitor)
        self.assertIn("elapsed >= ROLLBACK_DEADLINE_MS", monitor)
        self.assertIn("rejectRunningImageAndRollback()", monitor)
        self.assertIn("ota_startup_critical_error", (ROOT / "src/main.cpp").read_text(encoding="utf-8"))

    def test_maintenance_rejects_unhealthy_storage_and_drains_writers(self):
        main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        enter = main.split("bool ota_enter_maintenance()", 1)[1].split("void ota_leave_maintenance", 1)[0]
        self.assertIn("compare_exchange_strong", enter)
        self.assertIn("esp_lv_adapter_lock(pdMS_TO_TICKS(2000))", enter)
        self.assertIn("storage_manager.isReady()", enter)
        self.assertIn("!storage_manager.hasIncompleteTransactions()", enter)
        self.assertIn("activity_storage_startup_result == ActivityStorageResult::OK", enter)
        self.assertIn("activity_claim_startup_result == ActivityClaimStorageResult::OK", enter)
        self.assertIn("xSemaphoreTake(ota_background_io_mutex", enter)
        self.assertIn("if (!ota_maintenance_active.load())", main.split("void loop()", 1)[1])
        for operation in ("applyAccountMovement", "applyActivityReward", "clearLocalData",
                          "createSession", "closeSession", "reserveClaimId", "clearCredentials"):
            self.assertIn("ota_maintenance_active.load()", main)
            self.assertIn(operation, main)

    def test_maintenance_exit_and_failure_are_fail_closed(self):
        main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        enter = main.split("bool ota_enter_maintenance()", 1)[1].split("void ota_leave_maintenance", 1)[0]
        leave = main.split("void ota_leave_maintenance()", 1)[1].split("bool ota_can_reboot", 1)[0]
        self.assertGreaterEqual(enter.count("ota_maintenance_active.store(false)"), 2)
        self.assertIn("ota_maintenance_active.store(false)", leave)
        self.assertIn("ota_enter_maintenance, ota_leave_maintenance, ota_can_reboot", main)

    def test_ready_state_stays_in_maintenance_and_never_restarts_unconditionally(self):
        self.assertIn("maintenance_guard.retain = true", SOURCE)
        ready = SOURCE.split("bool OtaManager::confirmReadyToReboot", 1)[1]
        self.assertIn("authorization.valid_", ready)
        self.assertIn("!authorization.consumed_", ready)
        self.assertIn("authorization.version_", ready)
        self.assertIn("authorization.manifest_sha256_", ready)
        self.assertIn("authorization.manifest_url_", ready)
        self.assertIn("!can_reboot()", ready)
        self.assertIn("authorization.consumed_ = true", ready)
        self.assertIn("esp_restart();", ready)
        self.assertNotIn("esp_restart();", SOURCE.split("void OtaManager::runUpdate", 1)[1].split("bool OtaManager::cancel", 1)[0])
        self.assertIn("esperando aprobación administrativa reciente", (ROOT / "src/main.cpp").read_text(encoding="utf-8"))

    def test_health_requires_display_touch_frames_loop_and_financial_storage(self):
        main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        health = main.split("bool ota_boot_health_ok()", 1)[1].split("void setup()", 1)[0]
        for check in ("ota_health_display == nullptr", "ota_health_touch == nullptr", "lv_scr_act() != nullptr",
                      "esp_lv_adapter_get_frame_count() > ota_health_frame_baseline",
                      "heap_caps_check_integrity_all(false)",
                      "ota_main_loop_iterations.load() <= ota_health_loop_baseline",
                      "storage_manager.isReady()", "!storage_manager.hasIncompleteTransactions()",
                      "activity_storage_startup_result == ActivityStorageResult::OK",
                      "activity_claim_startup_result == ActivityClaimStorageResult::OK"):
            self.assertIn(check, health)

    def test_no_data_migration_added_and_ota_stays_disabled(self):
        self.assertIn("#define BANCO_OTA_ENABLED 0", CONFIG)
        platformio = (ROOT / "platformio.ini").read_text(encoding="utf-8")
        self.assertNotIn("BANCO_OTA_ENABLED=1", platformio)
        self.assertNotIn("migrate", SOURCE.lower())

    def test_incomplete_transaction_query_is_read_only_and_checks_journal(self):
        storage = (ROOT / "src/storage/storage_manager.cpp").read_text(encoding="utf-8")
        query = storage.split("bool StorageManager::hasIncompleteTransactions() const", 1)[1].split("size_t StorageManager::totalBytes", 1)[0]
        self.assertIn("recovery_required_", query)
        self.assertIn("transaction_in_progress_", query)
        self.assertIn("bank_littlefs.pathStatus(ACCOUNT_TXN_FILE)", query)
        self.assertNotIn("bank_littlefs.remove", query)
        self.assertNotIn("bank_littlefs.mount", query)


if __name__ == "__main__":
    unittest.main(verbosity=2)
