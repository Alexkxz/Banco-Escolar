#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <vector>

enum class WiFiState : uint8_t { DISCONNECTED, CONNECTING, CONNECTED, ERROR };

enum class WiFiManualResult : uint8_t {
    NONE,
    CONNECTING,
    SUCCEEDED,
    FAILED,
    SAVE_FAILED,
    CANCELED
};

struct WiFiSnapshot {
    WiFiState state;
    bool connected;
    char ssid[33];
    char ip[16];
    int32_t rssi;
};

struct WifiNetworkInfo {
    String ssid;
    int32_t rssi = 0;
    bool secured = false;
    uint8_t channel = 0;
};

class WiFiManager {
public:
    void begin();
    void update();
    WiFiSnapshot snapshot() const;
    WiFiState state() const;
    bool isConnected() const;

    bool hasSavedCredentials() const;
    bool startScan();
    bool isScanInProgress() const;
    bool hasScanCompleted() const;
    bool scanFailed() const;
    size_t getScanResultCount() const;
    WifiNetworkInfo getScanResult(size_t index) const;
    uint32_t scanRevision() const;

    bool connectToNetwork(const String &ssid, const String &password);
    bool retryPendingConnection();
    void cancelPendingConnection();
    WiFiManualResult manualConnectionResult() const;

    bool saveCredentials(const String &ssid, const String &password);
    bool loadCredentials();
    void clearCredentials();

private:
    void setState(WiFiState state);
    void startAutomaticAttempt(uint32_t now);
    void startManualAttempt(uint32_t now);
    void refreshConnectedData();
    void finishScan(int16_t result_count);
    void failManualAttempt(uint32_t now);
    bool loadCredentialsUnlocked();
    bool saveCredentialsUnlocked(const String &ssid, const String &password);

    mutable SemaphoreHandle_t mutex_ = nullptr;
    WiFiState state_ = WiFiState::DISCONNECTED;
    WiFiManualResult manual_result_ = WiFiManualResult::NONE;
    uint32_t attempt_started_at_ = 0;
    uint32_t next_attempt_at_ = 0;
    char ssid_[33] = {};
    char ip_[16] = {};
    int32_t rssi_ = 0;

    Preferences preferences_;
    bool preferences_ready_ = false;
    bool saved_credentials_valid_ = false;
    bool fallback_suppressed_ = false;
    String saved_ssid_;
    String saved_password_;
    String active_ssid_;
    String active_password_;
    String pending_ssid_;
    String pending_password_;
    bool manual_attempt_pending_ = false;

    bool scan_in_progress_ = false;
    bool scan_completed_ = false;
    bool scan_failed_ = false;
    uint32_t scan_revision_ = 0;
    std::vector<WifiNetworkInfo> scan_results_;
};

extern WiFiManager wifi_manager;
