#include "ota_manager.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_app_desc.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <mbedtls/pk.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "ota_config.h"

namespace {
constexpr size_t MANIFEST_MAX_BYTES = 2048;
constexpr size_t IO_CHUNK_BYTES = 1024;
constexpr uint32_t HTTP_TIMEOUT_MS = 10000;
constexpr size_t TASK_STACK_BYTES = 12288;
constexpr BaseType_t TASK_PRIORITY = 1;
constexpr BaseType_t TASK_CORE = 1;
// The operator needs time to inspect the panel and perform both input checks.
// Defer the first health decision until the same three-minute fail-closed limit.
constexpr uint32_t ROLLBACK_HEALTH_WINDOW_MS = 180000;
constexpr uint32_t ROLLBACK_DEADLINE_MS = 180000;
constexpr uint32_t OTA0_ADDRESS = 0x00010000;
constexpr uint32_t OTA1_ADDRESS = 0x00650000;
constexpr uint32_t OTA_SLOT_SIZE = 0x00640000;
constexpr uint32_t OTADATA_ADDRESS = 0x0000E000;
constexpr uint32_t OTADATA_SIZE = 0x00002000;
constexpr uint32_t FILESYSTEM_ADDRESS = 0x00C90000;
constexpr uint32_t FILESYSTEM_SIZE = 0x00360000;
constexpr uint32_t COREDUMP_ADDRESS = 0x00FF0000;
constexpr uint32_t COREDUMP_SIZE = 0x00010000;

bool same_partition(const esp_partition_t *a, const esp_partition_t *b)
{
    return a != nullptr && b != nullptr && a->type == b->type && a->subtype == b->subtype &&
           a->address == b->address && a->size == b->size;
}

bool normal_profile_has_expected_layout(const esp_partition_t *running)
{
#if BANCO_OTA_ENABLED
    (void)running;
    return false;
#else
    const esp_partition_t *ota0 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
    const esp_partition_t *ota1 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr);
    const esp_partition_t *nvs = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, nullptr);
    const esp_partition_t *otadata = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
    const esp_partition_t *filesystem = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
    const esp_partition_t *coredump = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, nullptr);
    if (running == nullptr || ota0 == nullptr || ota1 == nullptr || nvs == nullptr ||
        otadata == nullptr || filesystem == nullptr || coredump == nullptr ||
        !same_partition(running, ota0) || running->address != OTA0_ADDRESS ||
        running->size != OTA_SLOT_SIZE || ota1->type != ESP_PARTITION_TYPE_APP ||
        ota1->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_1 || ota1->address != OTA1_ADDRESS ||
        ota1->size != OTA_SLOT_SIZE || nvs->type != ESP_PARTITION_TYPE_DATA ||
        nvs->subtype != ESP_PARTITION_SUBTYPE_DATA_NVS || nvs->address != 0x9000 || nvs->size != 0x5000 ||
        otadata->type != ESP_PARTITION_TYPE_DATA || otadata->subtype != ESP_PARTITION_SUBTYPE_DATA_OTA ||
        otadata->address != OTADATA_ADDRESS || otadata->size != OTADATA_SIZE ||
        filesystem->type != ESP_PARTITION_TYPE_DATA ||
        filesystem->subtype != ESP_PARTITION_SUBTYPE_DATA_SPIFFS ||
        filesystem->address != FILESYSTEM_ADDRESS || filesystem->size != FILESYSTEM_SIZE ||
        coredump->type != ESP_PARTITION_TYPE_DATA ||
        coredump->subtype != ESP_PARTITION_SUBTYPE_DATA_COREDUMP ||
        coredump->address != COREDUMP_ADDRESS || coredump->size != COREDUMP_SIZE) return false;

    size_t count = 0;
    esp_partition_iterator_t iterator = esp_partition_find(
        ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);
    while (iterator != nullptr) {
        ++count;
        iterator = esp_partition_next(iterator);
    }
    return count == 6;
#endif
}

bool https_url(const String &url)
{
    if (!url.startsWith("https://") || url.length() > 512) return false;
    if (url.indexOf('@') >= 0 || url.indexOf('\\') >= 0 || url.indexOf('#') >= 0) return false;
    for (size_t i = 0; i < url.length(); ++i) {
        const char c = url[i];
        if (c <= 0x20 || c >= 0x7f) return false;
        if (c == '%') {
            if (i + 2 >= url.length() || !isxdigit(url[i + 1]) || !isxdigit(url[i + 2])) return false;
            i += 2;
        }
    }

    const int host_start = 8;
    int authority_end = url.indexOf('/', host_start);
    const int query_start = url.indexOf('?', host_start);
    if (authority_end < 0 || (query_start >= 0 && query_start < authority_end)) authority_end = query_start;
    if (authority_end < 0) authority_end = url.length();
    const String authority = url.substring(host_start, authority_end);
    if (authority.isEmpty()) return false;

    String host;
    String port;
    if (authority[0] == '[') {
        const int close = authority.indexOf(']');
        if (close <= 1) return false;
        host = authority.substring(1, close);
        if (host.indexOf(':') < 0) return false;
        for (size_t i = 0; i < host.length(); ++i) {
            const char c = host[i];
            if (!(isxdigit(c) || c == ':' || c == '.')) return false;
        }
        if (close + 1 < authority.length()) {
            if (authority[close + 1] != ':') return false;
            port = authority.substring(close + 2);
        }
    } else {
        const int colon = authority.lastIndexOf(':');
        host = colon < 0 ? authority : authority.substring(0, colon);
        if (colon >= 0) port = authority.substring(colon + 1);
        if (host.isEmpty() || host.length() > 253 || host[0] == '.' || host[host.length() - 1] == '.' ||
            host.indexOf("..") >= 0) return false;
        bool label_start = true;
        for (size_t i = 0; i < host.length(); ++i) {
            const char c = host[i];
            if (c == '.') {
                if (label_start || host[i - 1] == '-') return false;
                label_start = true;
            } else if (isalnum(c) || c == '-') {
                if (label_start && c == '-') return false;
                label_start = false;
            } else return false;
        }
        if (host[host.length() - 1] == '-') return false;
    }
    if (!port.isEmpty()) {
        uint32_t port_value = 0;
        for (size_t i = 0; i < port.length(); ++i) {
            if (!isdigit(port[i])) return false;
            port_value = port_value * 10U + static_cast<uint32_t>(port[i] - '0');
            if (port_value > 65535U) return false;
        }
        if (port_value == 0) return false;
    } else if (authority[authority.length() - 1] == ':') return false;

    for (int i = authority_end; i < static_cast<int>(url.length()); ++i) {
        const char c = url[i];
        if (!(isalnum(c) || strchr("-._~:/?[]!$&'()*+,;=%", c) != nullptr)) return false;
    }
    return true;
}

bool valid_version(const char *text)
{
    if (text == nullptr || *text == '\0' || strlen(text) > 23) return false;
    unsigned parts = 0;
    bool digit = false;
    bool leading_zero = false;
    size_t component_length = 0;
    for (const char *p = text; *p; ++p) {
        if (*p >= '0' && *p <= '9') {
            if (!digit) { leading_zero = (*p == '0'); component_length = 0; }
            else if (leading_zero) return false;
            digit = true;
            if (++component_length > 10) return false;
        }
        else if (*p == '.' && digit) { ++parts; digit = false; }
        else return false;
    }
    return digit && parts == 2;
}

bool parse_version(const char *text, uint32_t &major, uint32_t &minor, uint32_t &patch)
{
    if (!valid_version(text)) return false;
    char copy[24];
    strlcpy(copy, text, sizeof(copy));
    char *save = nullptr;
    char *part = strtok_r(copy, ".", &save);
    uint32_t values[3] = {};
    for (size_t i = 0; i < 3 && part != nullptr; ++i) {
        char *end = nullptr;
        errno = 0;
        const unsigned long value = strtoul(part, &end, 10);
        if (errno == ERANGE || end == part || *end != '\0' || value > UINT32_MAX) return false;
        values[i] = static_cast<uint32_t>(value);
        part = strtok_r(nullptr, ".", &save);
    }
    if (part != nullptr) return false;
    major = values[0]; minor = values[1]; patch = values[2];
    return true;
}

bool newer_version(const char *candidate, const char *current)
{
    uint32_t ca, cb, cc, oa, ob, oc;
    if (!parse_version(candidate, ca, cb, cc) || !parse_version(current, oa, ob, oc)) return false;
    if (ca != oa) return ca > oa;
    if (cb != ob) return cb > ob;
    return cc > oc;
}

bool valid_sha256_hex(const char *value)
{
    if (value == nullptr || strlen(value) != 64) return false;
    for (const char *p = value; *p; ++p) {
        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))) return false;
    }
    return true;
}

bool signature_valid(const char *canonical, size_t canonical_length, const char *encoded_signature)
{
    uint8_t signature[128] = {};
    size_t signature_length = 0;
    const int decode_result = mbedtls_base64_decode(signature, sizeof(signature), &signature_length,
        reinterpret_cast<const unsigned char *>(encoded_signature), strlen(encoded_signature));
    if (decode_result != 0 || signature_length == 0) return false;

    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
    bool valid = false;
    if (mbedtls_pk_parse_public_key(&key,
            reinterpret_cast<const unsigned char *>(BANCO_OTA_FIRMWARE_PUBLIC_KEY_PEM),
            strlen(BANCO_OTA_FIRMWARE_PUBLIC_KEY_PEM) + 1) == 0) {
        const auto key_type = mbedtls_pk_get_type(&key);
        if ((key_type == MBEDTLS_PK_ECKEY || key_type == MBEDTLS_PK_ECDSA) &&
            mbedtls_pk_ec(key)->MBEDTLS_PRIVATE(grp).id == MBEDTLS_ECP_DP_SECP256R1) {
            uint8_t digest[32] = {};
            const mbedtls_md_info_t *sha256 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
            if (sha256 != nullptr && mbedtls_md(sha256,
                    reinterpret_cast<const unsigned char *>(canonical), canonical_length, digest) == 0) {
                valid = mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, digest, sizeof(digest),
                                          signature, signature_length) == 0;
            }
        }
    }
    mbedtls_pk_free(&key);
    return valid;
}

String sha256_hex(const uint8_t digest[32]);

bool read_manifest(const String &url, JsonDocument &document, String &manifest_digest, OtaError &error)
{
    if (!https_url(url)) { error = OtaError::BAD_URL; return false; }
    WiFiClientSecure tls;
    tls.setCACert(BANCO_OTA_SERVER_ROOT_CA_PEM);
    tls.setTimeout(HTTP_TIMEOUT_MS);
    HTTPClient http;
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if (!http.begin(tls, url)) { error = OtaError::TLS_FAILED; return false; }
    const int status = http.GET();
    if (status != HTTP_CODE_OK) { http.end(); error = OtaError::HTTP_FAILED; return false; }
    const int content_length = http.getSize();
    if (content_length <= 0 || static_cast<size_t>(content_length) > MANIFEST_MAX_BYTES) {
        http.end(); error = OtaError::BAD_MANIFEST; return false;
    }
    const String body = http.getString();
    http.end();
    if (body.length() != static_cast<size_t>(content_length) ||
        deserializeJson(document, body, DeserializationOption::NestingLimit(3)) != DeserializationError::Ok) {
        error = OtaError::BAD_MANIFEST;
        return false;
    }
    uint8_t digest[32] = {};
    const mbedtls_md_info_t *sha256 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (sha256 == nullptr || mbedtls_md(sha256,
            reinterpret_cast<const unsigned char *>(body.c_str()), body.length(), digest) != 0) {
        error = OtaError::BAD_MANIFEST;
        return false;
    }
    manifest_digest = sha256_hex(digest);
    const JsonObjectConst object = document.as<JsonObjectConst>();
    if (object.isNull() || object.size() != 7 || !object["model"].is<const char *>() ||
        !object["version"].is<const char *>() || !object["size"].is<uint32_t>() ||
        !object["data_schema"].is<uint16_t>() ||
        !object["sha256"].is<const char *>() || !object["image_url"].is<const char *>() ||
        !object["signature"].is<const char *>()) {
        error = OtaError::BAD_MANIFEST;
        return false;
    }
    return true;
}

String sha256_hex(const uint8_t digest[32])
{
    static const char HEX_DIGITS[] = "0123456789abcdef";
    char output[65];
    for (size_t i = 0; i < 32; ++i) {
        output[i * 2] = HEX_DIGITS[digest[i] >> 4];
        output[i * 2 + 1] = HEX_DIGITS[digest[i] & 0x0F];
    }
    output[64] = '\0';
    return String(output);
}
}

OtaManager ota_manager;

// Arduino core normally confirms a pending OTA image from initArduino().
// Defer that decision whenever bootloader rollback is enabled so the
// application can verify startup health even in OTA-disabled test builds.
extern "C" bool verifyRollbackLater()
{
#if CONFIG_APP_ROLLBACK_ENABLE
    // This controls Arduino core's initArduino() auto-confirm behavior, not
    // whether network updates are enabled. Always defer while rollback exists.
    return true;
#else
    return false;
#endif
}

bool OtaManager::begin()
{
#if CONFIG_APP_ROLLBACK_ENABLE
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!verifyRollbackLater()) {
        boot_validation_state_ = OtaBootValidationState::NOT_PENDING;
        boot_validation_error_ = ESP_OK;
    } else if (running == nullptr) {
        boot_validation_state_ = OtaBootValidationState::STATE_UNKNOWN;
        boot_validation_error_ = ESP_ERR_NOT_FOUND;
    } else {
        esp_ota_img_states_t image_state;
        boot_validation_error_ = esp_ota_get_state_partition(running, &image_state);
        if (boot_validation_error_ != ESP_OK) {
            boot_validation_state_ = OtaBootValidationState::STATE_UNKNOWN;
        } else if (image_state == ESP_OTA_IMG_PENDING_VERIFY) {
            boot_validation_state_ = OtaBootValidationState::PENDING_VERIFY;
        } else if (image_state == ESP_OTA_IMG_VALID) {
#if !BANCO_OTA_ENABLED
            const esp_partition_t *selected = esp_ota_get_boot_partition();
            if (running->type == ESP_PARTITION_TYPE_APP &&
                running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 &&
                running->address == OTA0_ADDRESS && running->size == OTA_SLOT_SIZE &&
                same_partition(selected, running) && normal_profile_has_expected_layout(running)) {
                boot_validation_state_ = OtaBootValidationState::NOT_PENDING;
            } else {
                boot_validation_state_ = OtaBootValidationState::STATE_UNSAFE;
                boot_validation_error_ = ESP_ERR_INVALID_STATE;
            }
#else
            boot_validation_state_ = OtaBootValidationState::NOT_PENDING;
#endif
        } else if (image_state == ESP_OTA_IMG_UNDEFINED) {
#if !BANCO_OTA_ENABLED
            const esp_partition_t *selected = esp_ota_get_boot_partition();
            if (running->type == ESP_PARTITION_TYPE_APP &&
                running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 &&
                running->address == OTA0_ADDRESS && running->size == OTA_SLOT_SIZE &&
                same_partition(selected, running) && normal_profile_has_expected_layout(running)) {
                boot_validation_state_ = OtaBootValidationState::NOT_PENDING;
                boot_validation_error_ = ESP_OK;
            } else {
                boot_validation_state_ = OtaBootValidationState::STATE_UNSAFE;
                boot_validation_error_ = ESP_ERR_INVALID_STATE;
            }
#else
            boot_validation_state_ = OtaBootValidationState::STATE_UNSAFE;
            boot_validation_error_ = ESP_ERR_INVALID_STATE;
#endif
        } else if (image_state == ESP_OTA_IMG_NEW || image_state == ESP_OTA_IMG_INVALID ||
                   image_state == ESP_OTA_IMG_ABORTED) {
            boot_validation_state_ = OtaBootValidationState::STATE_UNSAFE;
            boot_validation_error_ = ESP_ERR_INVALID_STATE;
        } else {
            boot_validation_state_ = OtaBootValidationState::STATE_UNSAFE;
            boot_validation_error_ = ESP_ERR_INVALID_STATE;
        }
    }
    pending_verify_started_ms_ = boot_validation_state_ == OtaBootValidationState::PENDING_VERIFY
        ? millis() : 0;
    boot_storage_preparation_attempted_ = false;
    rollback_attempted_ = false;
#else
    boot_validation_state_ = OtaBootValidationState::NOT_PENDING;
    boot_validation_error_ = ESP_OK;
#endif
    if (mutex_ == nullptr) mutex_ = xSemaphoreCreateMutex();
    if (mutex_ == nullptr) return false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    snapshot_.state = OtaState::IDLE;
    snapshot_.error = OtaError::NONE;
    strlcpy(snapshot_.current_version, BANCO_OTA_CURRENT_VERSION, sizeof(snapshot_.current_version));
    xSemaphoreGive(mutex_);
    return true;
}

bool OtaManager::bootValidationPending() const
{
    return boot_validation_state_ == OtaBootValidationState::PENDING_VERIFY;
}

bool OtaManager::bootValidationHoldRequired() const
{
    return boot_validation_state_ != OtaBootValidationState::NOT_PENDING;
}

OtaBootValidationState OtaManager::bootValidationState() const
{
    return boot_validation_state_;
}

esp_err_t OtaManager::bootValidationError() const
{
    return boot_validation_error_;
}

void OtaManager::update()
{
    if (boot_validation_state_ != OtaBootValidationState::PENDING_VERIFY) return;
    const uint32_t elapsed = millis() - pending_verify_started_ms_;
    if (!boot_storage_preparation_attempted_ && elapsed >= ROLLBACK_HEALTH_WINDOW_MS &&
        boot_health_hook_ != nullptr && boot_health_hook_()) {
        boot_storage_preparation_attempted_ = true;
        if (boot_storage_prepare_hook_ == nullptr || !boot_storage_prepare_hook_()) {
            boot_validation_error_ = ESP_FAIL;
            Serial.printf("[OTA] Falló la preparación de LittleFS antes de confirmar (0x%x); solicitando rollback\n",
                          static_cast<unsigned>(boot_validation_error_));
            rejectRunningImageAndRollback();
            return;
        }
        // Keep the school-data gate closed through this second health pass and
        // the confirmation call. The preparation hook only mounts and reads;
        // it must not write school files, recover journals, format, or grow.
        if (!boot_health_hook_()) {
            boot_validation_error_ = ESP_ERR_INVALID_STATE;
            Serial.println("[OTA] Salud insuficiente después de preparar LittleFS; solicitando rollback");
            rejectRunningImageAndRollback();
            return;
        }
        if (confirmRunningImageHealthy()) return;
        Serial.printf("[OTA] No se pudo confirmar la imagen (0x%x); continúa bloqueada y pendiente de rollback\n",
                      static_cast<unsigned>(boot_validation_error_));
    }
    if (elapsed >= ROLLBACK_DEADLINE_MS && !rollback_attempted_) {
        Serial.println("[OTA] Venció la validación pendiente; solicitando rollback");
        rejectRunningImageAndRollback();
    }
}

bool OtaManager::setBootHealthHook(bool (*healthy)())
{
    if (mutex_ == nullptr) return false;
    boot_health_hook_ = healthy;
    return true;
}

bool OtaManager::setBootStoragePreparationHook(bool (*prepare)())
{
    if (mutex_ == nullptr) return false;
    boot_storage_prepare_hook_ = prepare;
    return true;
}

bool OtaManager::setMaintenanceHooks(const OtaMaintenanceHooks &hooks)
{
    if (mutex_ == nullptr || (hooks.enter == nullptr) != (hooks.leave == nullptr) ||
        (hooks.enter != nullptr && hooks.can_reboot == nullptr)) return false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (worker_ != nullptr || snapshot_.state == OtaState::READY_TO_REBOOT) {
        xSemaphoreGive(mutex_); return false;
    }
    maintenance_ = hooks;
    xSemaphoreGive(mutex_);
    return true;
}

bool OtaManager::requestUpdate(const OtaAuthorization &authorization, const char *manifest_url)
{
    if (mutex_ == nullptr || manifest_url == nullptr) return false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (worker_ != nullptr || snapshot_.state == OtaState::READY_TO_REBOOT ||
        snapshot_.state == OtaState::CHECKING ||
        snapshot_.state == OtaState::DOWNLOADING || snapshot_.state == OtaState::VERIFYING) {
        snapshot_.error = OtaError::BUSY;
        xSemaphoreGive(mutex_);
        return false;
    }
#if !BANCO_OTA_ENABLED
    snapshot_.state = OtaState::FAILED;
    snapshot_.error = OtaError::OTA_DISABLED;
    xSemaphoreGive(mutex_);
    return false;
#else
#if !CONFIG_APP_ROLLBACK_ENABLE
    snapshot_.state = OtaState::FAILED;
    snapshot_.error = OtaError::ROLLBACK_UNAVAILABLE;
    xSemaphoreGive(mutex_);
    return false;
#endif
    if (!authorization.valid_ || authorization.consumed_ || authorization.master_id_[0] == '\0' ||
        authorization.approval_id_[0] == '\0' ||
        static_cast<int32_t>(millis() - authorization.expires_at_ms_) >= 0 ||
        strcmp(authorization.manifest_url_, manifest_url) != 0) {
        snapshot_.state = OtaState::FAILED;
        snapshot_.error = OtaError::UNAUTHORIZED;
        xSemaphoreGive(mutex_);
        return false;
    }
    if (BANCO_OTA_CURRENT_VERSION[0] == '\0' || BANCO_OTA_SERVER_ROOT_CA_PEM[0] == '\0' ||
        BANCO_OTA_FIRMWARE_PUBLIC_KEY_PEM[0] == '\0') {
        snapshot_.state = OtaState::FAILED;
        snapshot_.error = OtaError::OTA_DISABLED;
        xSemaphoreGive(mutex_);
        return false;
    }
    if (!WiFi.isConnected()) {
        snapshot_.state = OtaState::FAILED; snapshot_.error = OtaError::WIFI_UNAVAILABLE;
        xSemaphoreGive(mutex_); return false;
    }
    if (maintenance_.enter == nullptr || maintenance_.leave == nullptr) {
        snapshot_.state = OtaState::FAILED; snapshot_.error = OtaError::MAINTENANCE_UNAVAILABLE;
        xSemaphoreGive(mutex_); return false;
    }
    authorization.consumed_ = true;
    strlcpy(pending_authorized_version_, authorization.version_, sizeof(pending_authorized_version_));
    strlcpy(pending_authorized_manifest_sha256_, authorization.manifest_sha256_,
            sizeof(pending_authorized_manifest_sha256_));
    strlcpy(pending_authorized_master_id_, authorization.master_id_, sizeof(pending_authorized_master_id_));
    strlcpy(pending_authorized_approval_id_, authorization.approval_id_, sizeof(pending_authorized_approval_id_));
    pending_authorization_expires_at_ms_ = authorization.expires_at_ms_;
    cancel_requested_.store(false, std::memory_order_relaxed);
    activation_committed_ = false;
    pending_manifest_url_ = manifest_url;
    BaseType_t result = xTaskCreatePinnedToCore(workerEntry, "ota_worker", TASK_STACK_BYTES, this,
                                                  TASK_PRIORITY, &worker_, TASK_CORE);
    if (result != pdPASS) {
        worker_ = nullptr; snapshot_.state = OtaState::FAILED; snapshot_.error = OtaError::FLASH_ERROR;
        xSemaphoreGive(mutex_); return false;
    }
    xSemaphoreGive(mutex_);
    return true;
#endif
}

void OtaManager::workerEntry(void *context)
{
    auto *manager = static_cast<OtaManager *>(context);
    manager->runUpdate(manager->pending_manifest_url_);
    xSemaphoreTake(manager->mutex_, portMAX_DELAY);
    manager->worker_ = nullptr;
    xSemaphoreGive(manager->mutex_);
    vTaskDelete(nullptr);
}

void OtaManager::setState(OtaState state, OtaError error)
{
    xSemaphoreTake(mutex_, portMAX_DELAY);
    snapshot_.state = state;
    snapshot_.error = error;
    xSemaphoreGive(mutex_);
}

bool OtaManager::updateProgress(uint32_t received, uint32_t total)
{
    xSemaphoreTake(mutex_, portMAX_DELAY);
    snapshot_.bytes_received = received;
    snapshot_.image_size = total;
    const bool canceled = cancel_requested_.load(std::memory_order_relaxed);
    xSemaphoreGive(mutex_);
    return !canceled;
}

void OtaManager::runUpdate(const String &manifest_url)
{
#if !BANCO_OTA_ENABLED
    (void)manifest_url;
    setState(OtaState::FAILED, OtaError::OTA_DISABLED);
#else
    setState(OtaState::CHECKING);
    OtaError error = OtaError::NONE;
    StaticJsonDocument<MANIFEST_MAX_BYTES> document;
    String downloaded_manifest_sha256;
    if (!read_manifest(manifest_url, document, downloaded_manifest_sha256, error)) {
        setState(OtaState::FAILED, error); return;
    }

    const char *model = document["model"] | "";
    const char *version = document["version"] | "";
    const char *hash = document["sha256"] | "";
    const char *image_url_value = document["image_url"] | "";
    const char *signature = document["signature"] | "";
    const uint32_t image_size = document["size"] | 0U;
    const uint16_t data_schema = document["data_schema"] | 0U;
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *target = running != nullptr ? esp_ota_get_next_update_partition(running) : nullptr;
    if (strcmp(model, BANCO_OTA_DEVICE_MODEL) != 0) { setState(OtaState::FAILED, OtaError::WRONG_MODEL); return; }
    if (!valid_version(version) || !valid_sha256_hex(hash) || !https_url(String(image_url_value)) ||
        strlen(signature) == 0 || image_size == 0) {
        setState(OtaState::FAILED, OtaError::BAD_MANIFEST); return;
    }
    if (!newer_version(version, BANCO_OTA_CURRENT_VERSION)) {
        setState(OtaState::FAILED, OtaError::VERSION_REJECTED); return;
    }
    // This release line preserves the physically verified ota_0 recovery image.
    // Refuse an update unless ota_0 is running and ota_1 is the exact target.
    if (running == nullptr || target == nullptr || target == running ||
        running->type != ESP_PARTITION_TYPE_APP ||
        running->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0 || running->address != 0x10000U ||
        running->size != 0x640000U || target->type != ESP_PARTITION_TYPE_APP ||
        target->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_1 || target->address != 0x650000U ||
        target->size != 0x640000U) {
        setState(OtaState::FAILED, OtaError::PARTITION_UNAVAILABLE); return;
    }
    if (image_size > target->size) { setState(OtaState::FAILED, OtaError::IMAGE_TOO_LARGE); return; }

    char canonical[768];
    const int canonical_length = snprintf(canonical, sizeof(canonical), "%s\n%s\n%lu\n%s\n%s\n%u",
        model, version, static_cast<unsigned long>(image_size), hash, image_url_value,
        static_cast<unsigned>(data_schema));
    if (canonical_length <= 0 || static_cast<size_t>(canonical_length) >= sizeof(canonical) ||
        !signature_valid(canonical, static_cast<size_t>(canonical_length), signature)) {
        setState(OtaState::FAILED, OtaError::BAD_SIGNATURE); return;
    }
    if (data_schema != BANCO_OTA_DATA_SCHEMA_VERSION) {
        setState(OtaState::FAILED, OtaError::DATA_INCOMPATIBLE); return;
    }
    if (strcmp(version, pending_authorized_version_) != 0 ||
        downloaded_manifest_sha256 != pending_authorized_manifest_sha256_ ||
        static_cast<int32_t>(millis() - pending_authorization_expires_at_ms_) >= 0) {
        setState(OtaState::FAILED, OtaError::UNAUTHORIZED); return;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    strlcpy(snapshot_.target_version, version, sizeof(snapshot_.target_version));
    snapshot_.image_size = image_size;
    xSemaphoreGive(mutex_);
    if (cancel_requested_.load(std::memory_order_relaxed)) { setState(OtaState::FAILED, OtaError::CANCELLED); return; }

    if (!maintenance_.enter()) { setState(OtaState::FAILED, OtaError::MAINTENANCE_UNAVAILABLE); return; }
    struct MaintenanceGuard {
        OtaMaintenanceHooks hooks;
        bool retain = false;
        ~MaintenanceGuard() { if (hooks.leave && !retain) hooks.leave(); }
    } maintenance_guard{maintenance_};

    setState(OtaState::DOWNLOADING);
    WiFiClientSecure tls;
    tls.setCACert(BANCO_OTA_SERVER_ROOT_CA_PEM);
    tls.setTimeout(HTTP_TIMEOUT_MS);
    HTTPClient http;
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if (!http.begin(tls, String(image_url_value))) { setState(OtaState::FAILED, OtaError::TLS_FAILED); return; }
    const int status = http.GET();
    if (status != HTTP_CODE_OK) { http.end(); setState(OtaState::FAILED, OtaError::HTTP_FAILED); return; }
    const int content_length = http.getSize();
    if (content_length <= 0 || static_cast<uint32_t>(content_length) != image_size) {
        http.end(); setState(OtaState::FAILED, OtaError::INCOMPLETE_IMAGE); return;
    }
    if (cancel_requested_.load(std::memory_order_relaxed)) { http.end(); setState(OtaState::FAILED, OtaError::CANCELLED); return; }

    esp_ota_handle_t handle = 0;
    if (esp_ota_begin(target, image_size, &handle) != ESP_OK) {
        http.end(); setState(OtaState::FAILED, OtaError::FLASH_ERROR); return;
    }
    bool ota_open = true;
    auto abort_ota = [&]() { if (ota_open) { esp_ota_abort(handle); ota_open = false; } };
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    bool hash_open = mbedtls_sha256_starts(&sha, 0) == 0;
    uint8_t buffer[IO_CHUNK_BYTES];
    uint32_t received = 0;
    WiFiClient *stream = http.getStreamPtr();
    uint32_t last_data_ms = millis();
    bool failed = false;
    while (received < image_size) {
        if (cancel_requested_.load(std::memory_order_relaxed)) { error = OtaError::CANCELLED; failed = true; break; }
        const int available = stream->available();
        if (available <= 0) {
            if (!http.connected() || millis() - last_data_ms > HTTP_TIMEOUT_MS) {
                error = OtaError::INCOMPLETE_IMAGE; failed = true; break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        const size_t wanted = std::min(static_cast<size_t>(available),
                                       std::min(sizeof(buffer), image_size - received));
        const int count = stream->readBytes(buffer, wanted);
        if (count <= 0) continue;
        last_data_ms = millis();
        if (!hash_open || mbedtls_sha256_update(&sha, buffer, static_cast<size_t>(count)) != 0 ||
            esp_ota_write(handle, buffer, static_cast<size_t>(count)) != ESP_OK) {
            error = OtaError::FLASH_ERROR; failed = true; break;
        }
        received += static_cast<uint32_t>(count);
        if (!updateProgress(received, image_size)) { error = OtaError::CANCELLED; failed = true; break; }
        vTaskDelay(1);
    }
    http.end();
    if (failed || received != image_size) {
        abort_ota();
        if (hash_open) mbedtls_sha256_free(&sha);
        setState(OtaState::FAILED, failed ? error : OtaError::INCOMPLETE_IMAGE);
        return;
    }

    setState(OtaState::VERIFYING);
    uint8_t digest[32] = {};
    const bool hash_ok = hash_open && mbedtls_sha256_finish(&sha, digest) == 0 && sha256_hex(digest) == hash;
    if (hash_open) mbedtls_sha256_free(&sha);
    if (!hash_ok) { abort_ota(); setState(OtaState::FAILED, OtaError::HASH_MISMATCH); return; }
    if (cancel_requested_.load(std::memory_order_relaxed)) { abort_ota(); setState(OtaState::FAILED, OtaError::CANCELLED); return; }
    if (static_cast<int32_t>(millis() - pending_authorization_expires_at_ms_) >= 0) {
        abort_ota(); setState(OtaState::FAILED, OtaError::UNAUTHORIZED); return;
    }
    if (esp_ota_end(handle) != ESP_OK) { ota_open = false; setState(OtaState::FAILED, OtaError::IMAGE_INVALID); return; }
    ota_open = false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool canceled_before_activation = cancel_requested_.load(std::memory_order_relaxed) ||
        static_cast<int32_t>(millis() - pending_authorization_expires_at_ms_) >= 0;
    if (!canceled_before_activation) activation_committed_ = true;
    xSemaphoreGive(mutex_);
    if (canceled_before_activation) {
        setState(OtaState::FAILED, cancel_requested_.load(std::memory_order_relaxed)
            ? OtaError::CANCELLED : OtaError::UNAUTHORIZED);
        return;
    }
    if (esp_ota_set_boot_partition(target) != ESP_OK) {
        setState(OtaState::FAILED, OtaError::FLASH_ERROR); return;
    }
    setState(OtaState::READY_TO_REBOOT);
    maintenance_guard.retain = true;
#endif
}

bool OtaManager::cancel()
{
    if (mutex_ == nullptr) return false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool cancellable = worker_ != nullptr && !activation_committed_ &&
        (snapshot_.state == OtaState::CHECKING || snapshot_.state == OtaState::DOWNLOADING ||
         snapshot_.state == OtaState::VERIFYING);
    if (cancellable) cancel_requested_.store(true, std::memory_order_relaxed);
    xSemaphoreGive(mutex_);
    return cancellable;
}

OtaSnapshot OtaManager::snapshot() const
{
    OtaSnapshot result = {};
    if (mutex_ == nullptr) return result;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    result = snapshot_;
    xSemaphoreGive(mutex_);
    return result;
}

bool OtaManager::confirmRunningImageHealthy()
{
#if CONFIG_APP_ROLLBACK_ENABLE
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (running == nullptr) {
        boot_validation_error_ = ESP_ERR_NOT_FOUND;
        Serial.printf("[OTA] No se pudo consultar la partición en ejecución para confirmar (0x%x)\n",
                      static_cast<unsigned>(boot_validation_error_));
        return false;
    }
    boot_validation_error_ = esp_ota_get_state_partition(running, &state);
    if (boot_validation_error_ != ESP_OK || state != ESP_OTA_IMG_PENDING_VERIFY) {
        if (boot_validation_error_ == ESP_OK) boot_validation_error_ = ESP_ERR_INVALID_STATE;
        Serial.printf("[OTA] Confirmación rechazada: estado pendiente no verificable (0x%x)\n",
                      static_cast<unsigned>(boot_validation_error_));
        return false;
    }
    boot_validation_error_ = esp_ota_mark_app_valid_cancel_rollback();
    if (boot_validation_error_ != ESP_OK) {
        Serial.printf("[OTA] Falló la confirmación de la imagen (0x%x)\n",
                      static_cast<unsigned>(boot_validation_error_));
        return false;
    }
    boot_validation_state_ = OtaBootValidationState::NOT_PENDING;
    return true;
#else
    return false;
#endif
}

bool OtaManager::rejectRunningImageAndRollback()
{
#if CONFIG_APP_ROLLBACK_ENABLE
    if (rollback_attempted_) return false;
    // Latch before querying/calling the bootloader. A persistent error must
    // not produce a tight retry or reset loop; the maintenance gate stays set.
    rollback_attempted_ = true;
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (running == nullptr) {
        boot_validation_error_ = ESP_ERR_NOT_FOUND;
        Serial.printf("[OTA] No se pudo consultar la partición en ejecución para rollback (0x%x)\n",
                      static_cast<unsigned>(boot_validation_error_));
        return false;
    }
    boot_validation_error_ = esp_ota_get_state_partition(running, &state);
    if (boot_validation_error_ != ESP_OK || state != ESP_OTA_IMG_PENDING_VERIFY) {
        if (boot_validation_error_ == ESP_OK) boot_validation_error_ = ESP_ERR_INVALID_STATE;
        Serial.printf("[OTA] Rollback omitido: estado pendiente no verificable (0x%x); escrituras permanecen bloqueadas\n",
                      static_cast<unsigned>(boot_validation_error_));
        return false;
    }
    boot_validation_error_ = esp_ota_mark_app_invalid_rollback_and_reboot();
    if (boot_validation_error_ != ESP_OK) {
        Serial.printf("[OTA] No se pudo solicitar rollback (0x%x); mantenimiento permanece bloqueado\n",
                      static_cast<unsigned>(boot_validation_error_));
        return false;
    }
    return true;
#else
    return false;
#endif
}

bool OtaManager::confirmReadyToReboot(OtaAuthorization &authorization)
{
#if !BANCO_OTA_ENABLED
    (void)authorization;
    return false;
#else
    if (mutex_ == nullptr || maintenance_.can_reboot == nullptr) return false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    bool (*can_reboot)() = maintenance_.can_reboot;
    const bool capability_matches = snapshot_.state == OtaState::READY_TO_REBOOT &&
        authorization.valid_ && !authorization.consumed_ &&
        static_cast<int32_t>(millis() - authorization.expires_at_ms_) < 0 &&
        strcmp(authorization.version_, pending_authorized_version_) == 0 &&
        strcmp(authorization.manifest_sha256_, pending_authorized_manifest_sha256_) == 0 &&
        strcmp(authorization.master_id_, pending_authorized_master_id_) == 0 &&
        strcmp(authorization.approval_id_, pending_authorized_approval_id_) != 0 &&
        authorization.master_id_[0] != '\0' && authorization.approval_id_[0] != '\0' &&
        strcmp(authorization.manifest_url_, pending_manifest_url_.c_str()) == 0;
    xSemaphoreGive(mutex_);
    if (!capability_matches) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (snapshot_.state == OtaState::READY_TO_REBOOT) snapshot_.error = OtaError::UNAUTHORIZED;
        xSemaphoreGive(mutex_);
        return false;
    }
    if (can_reboot == nullptr || !can_reboot()) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (snapshot_.state == OtaState::READY_TO_REBOOT) snapshot_.error = OtaError::MAINTENANCE_UNAVAILABLE;
        xSemaphoreGive(mutex_);
        return false;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool still_ready = snapshot_.state == OtaState::READY_TO_REBOOT &&
        authorization.valid_ && !authorization.consumed_ &&
        static_cast<int32_t>(millis() - authorization.expires_at_ms_) < 0 &&
        strcmp(authorization.version_, pending_authorized_version_) == 0 &&
        strcmp(authorization.manifest_sha256_, pending_authorized_manifest_sha256_) == 0 &&
        strcmp(authorization.master_id_, pending_authorized_master_id_) == 0 &&
        strcmp(authorization.approval_id_, pending_authorized_approval_id_) != 0 &&
        authorization.master_id_[0] != '\0' && authorization.approval_id_[0] != '\0' &&
        strcmp(authorization.manifest_url_, pending_manifest_url_.c_str()) == 0;
    if (!still_ready) { xSemaphoreGive(mutex_); return false; }
    authorization.consumed_ = true;
    xSemaphoreGive(mutex_);
    esp_restart();
    return true;
#endif
}

