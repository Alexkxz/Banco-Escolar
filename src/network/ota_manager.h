#pragma once

#include <Arduino.h>
#include <atomic>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "ota_authorization.h"

enum class OtaState : uint8_t {
    IDLE,
    CHECKING,
    DOWNLOADING,
    VERIFYING,
    READY_TO_REBOOT,
    FAILED
};

enum class OtaError : uint8_t {
    NONE,
    OTA_DISABLED,
    UNAUTHORIZED,
    BUSY,
    WIFI_UNAVAILABLE,
    BAD_URL,
    TLS_FAILED,
    HTTP_FAILED,
    BAD_MANIFEST,
    BAD_SIGNATURE,
    WRONG_MODEL,
    VERSION_REJECTED,
    IMAGE_TOO_LARGE,
    PARTITION_UNAVAILABLE,
    INCOMPLETE_IMAGE,
    HASH_MISMATCH,
    IMAGE_INVALID,
    DATA_INCOMPATIBLE,
    FLASH_ERROR,
    CANCELLED,
    MAINTENANCE_UNAVAILABLE,
    ROLLBACK_UNAVAILABLE
};

enum class OtaBootValidationState : uint8_t {
    NOT_PENDING,
    PENDING_VERIFY,
    STATE_UNKNOWN,
    STATE_UNSAFE
};

struct OtaSnapshot {
    OtaState state;
    OtaError error;
    uint32_t bytes_received;
    uint32_t image_size;
    char current_version[24];
    char target_version[24];
};

struct OtaMaintenanceHooks {
    bool (*enter)() = nullptr;
    void (*leave)() = nullptr;
    bool (*can_reboot)() = nullptr;
};

class OtaManager {
public:
    bool begin();
    bool bootValidationPending() const;
    bool bootValidationHoldRequired() const;
    OtaBootValidationState bootValidationState() const;
    esp_err_t bootValidationError() const;
    void update();
    bool setBootHealthHook(bool (*healthy)());
    bool setBootStoragePreparationHook(bool (*prepare)());
    bool setMaintenanceHooks(const OtaMaintenanceHooks &hooks);
    bool requestUpdate(const OtaAuthorization &authorization, const char *manifest_url);
    bool cancel();
    OtaSnapshot snapshot() const;
    bool confirmRunningImageHealthy();
    bool rejectRunningImageAndRollback();
    bool confirmReadyToReboot(OtaAuthorization &authorization);

private:
    static void workerEntry(void *context);
    void runUpdate(const String &manifest_url);
    void setState(OtaState state, OtaError error = OtaError::NONE);
    bool updateProgress(uint32_t received, uint32_t total);

    mutable SemaphoreHandle_t mutex_ = nullptr;
    OtaSnapshot snapshot_ = {};
    OtaMaintenanceHooks maintenance_ = {};
    TaskHandle_t worker_ = nullptr;
    String pending_manifest_url_;
    char pending_authorized_version_[24] = {};
    char pending_authorized_manifest_sha256_[65] = {};
    char pending_authorized_master_id_[33] = {};
    char pending_authorized_approval_id_[65] = {};
    uint32_t pending_authorization_expires_at_ms_ = 0;
    std::atomic<bool> cancel_requested_{false};
    bool activation_committed_ = false;
    bool (*boot_health_hook_)() = nullptr;
    bool (*boot_storage_prepare_hook_)() = nullptr;
    uint32_t pending_verify_started_ms_ = 0;
    OtaBootValidationState boot_validation_state_ = OtaBootValidationState::STATE_UNKNOWN;
    esp_err_t boot_validation_error_ = ESP_ERR_INVALID_STATE;
    bool boot_storage_preparation_attempted_ = false;
    bool rollback_attempted_ = false;
};

extern OtaManager ota_manager;

