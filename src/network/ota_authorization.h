#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

// Output of a trusted LOGIN-R adapter. The adapter is responsible for checking
// the authenticated, current master session and atomically consuming approval_id.
struct OtaVerifiedMasterApproval {
    bool authenticated_session = false;
    bool session_current = false;
    bool administrator = false;
    bool firmware_update_permission = false;
    char master_id[33] = {};
    char approval_id[65] = {};
    char version[24] = {};
    char manifest_sha256[65] = {};
    char manifest_url[513] = {};
    // Must be translated by the device adapter to the ESP32 millis() domain.
    uint32_t expires_at_ms = 0;
};

class OtaAuthorizationProvider;
class OtaManager;

class OtaAuthorization {
public:
    OtaAuthorization() = default;
    OtaAuthorization(const OtaAuthorization &) = delete;
    OtaAuthorization &operator=(const OtaAuthorization &) = delete;

private:
    friend class OtaAuthorizationProvider;
    friend class OtaManager;
    bool valid_ = false;
    mutable bool consumed_ = false;
    uint32_t expires_at_ms_ = 0;
    char master_id_[33] = {};
    char approval_id_[65] = {};
    char version_[24] = {};
    char manifest_sha256_[65] = {};
    char manifest_url_[513] = {};
};

class OtaAuthorizationProvider {
public:
    using MasterApprovalVerifier = bool (*)(void *context, const uint8_t *proof, size_t proof_size,
        const char *version, const char *manifest_sha256, const char *manifest_url,
        OtaVerifiedMasterApproval &verified);

    explicit OtaAuthorizationProvider(MasterApprovalVerifier verifier = nullptr, void *context = nullptr)
        : verifier_(verifier), context_(context) {}

    // Fails closed without a LOGIN-R verifier. A verifier must validate a live
    // authenticated master session, admin role and exact firmware-update
    // permission; approval_id must be consumed atomically to prevent replay.
    bool issue(const uint8_t *proof, size_t proof_size, const char *version,
               const char *manifest_sha256, const char *manifest_url,
               uint32_t now_ms, OtaAuthorization &authorization) const
    {
        if (verifier_ == nullptr || proof == nullptr || proof_size == 0 ||
            version == nullptr || manifest_sha256 == nullptr || manifest_url == nullptr ||
            authorization.valid_ || strlen(version) == 0 || strlen(version) >= sizeof(authorization.version_) ||
            !valid_sha256(manifest_sha256) || strlen(manifest_url) == 0 ||
            strlen(manifest_url) >= sizeof(authorization.manifest_url_)) return false;

        OtaVerifiedMasterApproval result;
        if (!verifier_(context_, proof, proof_size, version, manifest_sha256, manifest_url, result) ||
            !result.authenticated_session || !result.session_current || !result.administrator ||
            !result.firmware_update_permission || !valid_id(result.master_id, sizeof(result.master_id), 32) ||
            !valid_id(result.approval_id, sizeof(result.approval_id), 64) ||
            !valid_text(result.version, sizeof(result.version)) ||
            !valid_sha256(result.manifest_sha256, sizeof(result.manifest_sha256)) ||
            !valid_text(result.manifest_url, sizeof(result.manifest_url)) ||
            strcmp(result.version, version) != 0 || strcmp(result.manifest_sha256, manifest_sha256) != 0 ||
            strcmp(result.manifest_url, manifest_url) != 0) return false;

        const int32_t remaining = static_cast<int32_t>(result.expires_at_ms - now_ms);
        if (remaining <= 0 || remaining > 120000) return false;

        strlcpy(authorization.master_id_, result.master_id, sizeof(authorization.master_id_));
        strlcpy(authorization.approval_id_, result.approval_id, sizeof(authorization.approval_id_));
        strlcpy(authorization.version_, result.version, sizeof(authorization.version_));
        strlcpy(authorization.manifest_sha256_, result.manifest_sha256, sizeof(authorization.manifest_sha256_));
        strlcpy(authorization.manifest_url_, result.manifest_url, sizeof(authorization.manifest_url_));
        authorization.expires_at_ms_ = result.expires_at_ms;
        authorization.valid_ = true;
        return true;
    }

private:
    static bool valid_id(const char *value, size_t capacity, size_t max_length)
    {
        const size_t length = strnlen(value, capacity);
        if (length == 0 || length > max_length || length == capacity) return false;
        for (size_t i = 0; i < length; ++i) {
            const char c = value[i];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
        }
        return true;
    }

    static bool valid_sha256(const char *value)
    {
        return valid_sha256(value, 65);
    }

    static bool valid_sha256(const char *value, size_t capacity)
    {
        if (value == nullptr || strnlen(value, capacity) != 64) return false;
        for (size_t i = 0; i < 64; ++i) {
            const char c = value[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        }
        return true;
    }

    static bool valid_text(const char *value, size_t capacity)
    {
        const size_t length = strnlen(value, capacity);
        return length > 0 && length < capacity;
    }

    MasterApprovalVerifier verifier_ = nullptr;
    void *context_ = nullptr;
};
