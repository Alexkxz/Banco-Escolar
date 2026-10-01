#include "wifi_manager.h"

#include <WiFi.h>
#include <algorithm>
#include <cstring>

#include "wifi_credentials.h"

namespace {
constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 5000;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 12000;
constexpr char WIFI_PREFERENCES_NAMESPACE[] = "banco_wifi";
constexpr char WIFI_SSID_KEY[] = "ssid";
constexpr char WIFI_PASSWORD_KEY[] = "password";

class SemaphoreGuard {
public:
    explicit SemaphoreGuard(SemaphoreHandle_t semaphore) : semaphore_(semaphore)
    {
        if (semaphore_) xSemaphoreTake(semaphore_, portMAX_DELAY);
    }
    ~SemaphoreGuard()
    {
        if (semaphore_) xSemaphoreGive(semaphore_);
    }
private:
    SemaphoreHandle_t semaphore_;
};
}

WiFiManager wifi_manager;

void WiFiManager::begin()
{
    if (mutex_ == nullptr) mutex_ = xSemaphoreCreateMutex();
    SemaphoreGuard guard(mutex_);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);
    preferences_ready_ = preferences_.begin(WIFI_PREFERENCES_NAMESPACE, false);
    loadCredentialsUnlocked();
    state_ = WiFiState::DISCONNECTED;
    next_attempt_at_ = 0;
}

void WiFiManager::setState(WiFiState state)
{
    if (state_ == state) return;
    state_ = state;
    if (state_ == WiFiState::CONNECTING) {
        Serial.printf("[WiFi] Conectando a: %s\n", ssid_);
    } else if (state_ == WiFiState::CONNECTED) {
        Serial.println("[WiFi] Conectado");
        Serial.printf("[WiFi] IP: %s\n", ip_);
        Serial.printf("[WiFi] RSSI: %d dBm\n", static_cast<int>(rssi_));
    } else if (state_ == WiFiState::DISCONNECTED) {
        Serial.println("[WiFi] Sin conexion");
    } else if (state_ == WiFiState::ERROR) {
        Serial.println("[WiFi] Error de conexion");
    }
}

bool WiFiManager::hasSavedCredentials() const
{
    SemaphoreGuard guard(mutex_);
    return saved_credentials_valid_;
}

bool WiFiManager::loadCredentials()
{
    SemaphoreGuard guard(mutex_);
    return loadCredentialsUnlocked();
}

bool WiFiManager::loadCredentialsUnlocked()
{
    if (!preferences_ready_) {
        preferences_ready_ = preferences_.begin(WIFI_PREFERENCES_NAMESPACE, false);
    }

    saved_credentials_valid_ = false;
    saved_ssid_ = "";
    saved_password_ = "";

    if (preferences_ready_ && preferences_.isKey(WIFI_SSID_KEY)) {
        const String stored_ssid = preferences_.getString(WIFI_SSID_KEY, "");
        if (stored_ssid.length() > 0) {
            saved_ssid_ = stored_ssid;
            saved_password_ = preferences_.getString(WIFI_PASSWORD_KEY, "");
            saved_credentials_valid_ = true;
            active_ssid_ = saved_ssid_;
            active_password_ = saved_password_;
            strncpy(ssid_, active_ssid_.c_str(), sizeof(ssid_) - 1);
            ssid_[sizeof(ssid_) - 1] = '\0';
            return true;
        }
    }

    if (!fallback_suppressed_ && WIFI_SSID[0] != '\0') {
        active_ssid_ = WIFI_SSID;
        active_password_ = WIFI_PASSWORD;
        strncpy(ssid_, active_ssid_.c_str(), sizeof(ssid_) - 1);
        ssid_[sizeof(ssid_) - 1] = '\0';
        return true;
    }

    active_ssid_ = "";
    active_password_ = "";
    ssid_[0] = '\0';
    return false;
}

bool WiFiManager::saveCredentials(const String &ssid, const String &password)
{
    SemaphoreGuard guard(mutex_);
    return saveCredentialsUnlocked(ssid, password);
}

bool WiFiManager::saveCredentialsUnlocked(const String &ssid, const String &password)
{
    if (ssid.length() == 0 || ssid.length() > 32 || password.length() > 63) return false;
    if (!preferences_ready_) {
        preferences_ready_ = preferences_.begin(WIFI_PREFERENCES_NAMESPACE, false);
    }
    if (!preferences_ready_) return false;

    const bool had_saved_credentials = saved_credentials_valid_;
    const String old_ssid = saved_ssid_;
    const String old_password = saved_password_;
    const size_t ssid_size = preferences_.putString(WIFI_SSID_KEY, ssid);
    const size_t password_size = preferences_.putString(WIFI_PASSWORD_KEY, password);
    const bool ssid_ok = (ssid_size == ssid.length() &&
                          preferences_.getString(WIFI_SSID_KEY, "") == ssid);
    const bool password_ok = password.length() == 0 ?
        (preferences_.isKey(WIFI_PASSWORD_KEY) && preferences_.getString(WIFI_PASSWORD_KEY, "") == password) :
        (password_size == password.length() && preferences_.getString(WIFI_PASSWORD_KEY, "") == password);
    if (!ssid_ok || !password_ok) {
        if (had_saved_credentials) {
            preferences_.putString(WIFI_SSID_KEY, old_ssid);
            preferences_.putString(WIFI_PASSWORD_KEY, old_password);
        } else {
            preferences_.remove(WIFI_SSID_KEY);
            preferences_.remove(WIFI_PASSWORD_KEY);
        }
        return false;
    }

    saved_ssid_ = ssid;
    saved_password_ = password;
    saved_credentials_valid_ = true;
    return true;
}

void WiFiManager::clearCredentials()
{
    SemaphoreGuard guard(mutex_);
    if (!preferences_ready_) {
        preferences_ready_ = preferences_.begin(WIFI_PREFERENCES_NAMESPACE, false);
    }
    if (preferences_ready_) {
        preferences_.remove(WIFI_SSID_KEY);
        preferences_.remove(WIFI_PASSWORD_KEY);
    }
    saved_credentials_valid_ = false;
    saved_ssid_ = "";
    saved_password_ = "";
    active_ssid_ = "";
    active_password_ = "";
    fallback_suppressed_ = true;
    pending_ssid_ = "";
    pending_password_ = "";
    manual_attempt_pending_ = false;
    manual_result_ = WiFiManualResult::CANCELED;
    WiFi.disconnect(false, false);
    ssid_[0] = '\0';
    ip_[0] = '\0';
    rssi_ = 0;
    next_attempt_at_ = UINT32_MAX;
    setState(WiFiState::DISCONNECTED);
}

bool WiFiManager::startScan()
{
    SemaphoreGuard guard(mutex_);
    if (scan_in_progress_ || manual_attempt_pending_) return false;
    if (state_ == WiFiState::CONNECTING) {
        WiFi.disconnect(false, false);
        setState(WiFiState::DISCONNECTED);
        next_attempt_at_ = millis() + WIFI_RECONNECT_INTERVAL_MS;
    }
    WiFi.scanDelete();
    scan_results_.clear();
    scan_completed_ = false;
    scan_failed_ = false;
    scan_in_progress_ = true;
    Serial.println("[WiFi] Escaneando redes");
    const int16_t scan_status = WiFi.scanNetworks(true, true);
    if (scan_status == WIFI_SCAN_FAILED) {
        scan_in_progress_ = false;
        scan_completed_ = true;
        scan_failed_ = true;
        ++scan_revision_;
        Serial.println("[WiFi] Error de escaneo");
        return false;
    }
    return true;
}

void WiFiManager::finishScan(int16_t result_count)
{
    scan_results_.clear();
    if (result_count < 0) {
        scan_failed_ = true;
    } else {
        for (int16_t i = 0; i < result_count; ++i) {
            const String found_ssid = WiFi.SSID(static_cast<uint8_t>(i));
            if (found_ssid.length() == 0) continue;

            WifiNetworkInfo found;
            found.ssid = found_ssid;
            found.rssi = WiFi.RSSI(static_cast<uint8_t>(i));
            found.secured = WiFi.encryptionType(static_cast<uint8_t>(i)) != WIFI_AUTH_OPEN;
            found.channel = WiFi.channel(static_cast<uint8_t>(i));

            auto duplicate = std::find_if(scan_results_.begin(), scan_results_.end(),
                [&found](const WifiNetworkInfo &item) { return item.ssid == found.ssid; });
            if (duplicate == scan_results_.end()) scan_results_.push_back(found);
            else if (found.rssi > duplicate->rssi) *duplicate = found;
        }
        std::sort(scan_results_.begin(), scan_results_.end(),
            [](const WifiNetworkInfo &left, const WifiNetworkInfo &right) { return left.rssi > right.rssi; });
    }

    scan_in_progress_ = false;
    scan_completed_ = true;
    ++scan_revision_;
    if (scan_failed_) Serial.println("[WiFi] Error de escaneo");
    else Serial.printf("[WiFi] Escaneo terminado: %u redes\n", static_cast<unsigned>(scan_results_.size()));
    WiFi.scanDelete();
}

bool WiFiManager::isScanInProgress() const { SemaphoreGuard guard(mutex_); return scan_in_progress_; }
bool WiFiManager::hasScanCompleted() const { SemaphoreGuard guard(mutex_); return scan_completed_; }
bool WiFiManager::scanFailed() const { SemaphoreGuard guard(mutex_); return scan_failed_; }
size_t WiFiManager::getScanResultCount() const { SemaphoreGuard guard(mutex_); return scan_results_.size(); }
WifiNetworkInfo WiFiManager::getScanResult(size_t index) const
{
    SemaphoreGuard guard(mutex_);
    return index < scan_results_.size() ? scan_results_[index] : WifiNetworkInfo{};
}
uint32_t WiFiManager::scanRevision() const { SemaphoreGuard guard(mutex_); return scan_revision_; }

bool WiFiManager::connectToNetwork(const String &ssid, const String &password)
{
    SemaphoreGuard guard(mutex_);
    if (ssid.length() == 0 || ssid.length() > 32 || password.length() > 63) return false;
    if (scan_in_progress_) {
        WiFi.scanDelete();
        scan_in_progress_ = false;
        scan_completed_ = false;
    }
    pending_ssid_ = ssid;
    pending_password_ = password;
    manual_attempt_pending_ = true;
    manual_result_ = WiFiManualResult::CONNECTING;
    startManualAttempt(millis());
    return true;
}

void WiFiManager::startManualAttempt(uint32_t now)
{
    if (pending_ssid_.length() == 0) return;
    WiFi.disconnect(false, false);
    strncpy(ssid_, pending_ssid_.c_str(), sizeof(ssid_) - 1);
    ssid_[sizeof(ssid_) - 1] = '\0';
    ip_[0] = '\0';
    rssi_ = 0;
    attempt_started_at_ = now;
    next_attempt_at_ = UINT32_MAX;
    setState(WiFiState::CONNECTING);
    WiFi.begin(pending_ssid_.c_str(), pending_password_.c_str());
}

bool WiFiManager::retryPendingConnection()
{
    SemaphoreGuard guard(mutex_);
    if (pending_ssid_.length() == 0 || manual_attempt_pending_) return false;
    manual_attempt_pending_ = true;
    manual_result_ = WiFiManualResult::CONNECTING;
    startManualAttempt(millis());
    return true;
}

void WiFiManager::cancelPendingConnection()
{
    SemaphoreGuard guard(mutex_);
    if (manual_attempt_pending_ || pending_ssid_.length() > 0) {
        WiFi.disconnect(false, false);
        manual_attempt_pending_ = false;
        pending_ssid_ = "";
        pending_password_ = "";
        manual_result_ = WiFiManualResult::CANCELED;
        ip_[0] = '\0';
        rssi_ = 0;
        ssid_[0] = '\0';
        next_attempt_at_ = active_ssid_.length() == 0 ? UINT32_MAX : millis() + WIFI_RECONNECT_INTERVAL_MS;
        setState(WiFiState::DISCONNECTED);
    }
}

WiFiManualResult WiFiManager::manualConnectionResult() const { SemaphoreGuard guard(mutex_); return manual_result_; }

void WiFiManager::startAutomaticAttempt(uint32_t now)
{
    if (active_ssid_.length() == 0 || scan_in_progress_) return;
    strncpy(ssid_, active_ssid_.c_str(), sizeof(ssid_) - 1);
    ssid_[sizeof(ssid_) - 1] = '\0';
    attempt_started_at_ = now;
    setState(WiFiState::CONNECTING);
    WiFi.begin(active_ssid_.c_str(), active_password_.c_str());
}

void WiFiManager::refreshConnectedData()
{
    WiFi.SSID().substring(0, sizeof(ssid_) - 1).toCharArray(ssid_, sizeof(ssid_));
    WiFi.localIP().toString().substring(0, sizeof(ip_) - 1).toCharArray(ip_, sizeof(ip_));
    rssi_ = WiFi.RSSI();
}

void WiFiManager::failManualAttempt(uint32_t now)
{
    WiFi.disconnect(false, false);
    manual_attempt_pending_ = false;
    manual_result_ = WiFiManualResult::FAILED;
    ip_[0] = '\0';
    rssi_ = 0;
    setState(WiFiState::ERROR);
    next_attempt_at_ = active_ssid_.length() == 0 ? UINT32_MAX : now + WIFI_RECONNECT_INTERVAL_MS;
}

void WiFiManager::update()
{
    SemaphoreGuard guard(mutex_);
    if (scan_in_progress_) {
        const int16_t result = WiFi.scanComplete();
        if (result != WIFI_SCAN_RUNNING) finishScan(result);
    }

    const uint32_t now = millis();
    if (fallback_suppressed_ && active_ssid_.length() == 0 && state_ == WiFiState::DISCONNECTED) {
        if (WiFi.status() == WL_CONNECTED) WiFi.disconnect(false, false);
        return;
    }
    if (WiFi.status() == WL_CONNECTED) {
        refreshConnectedData();
        if (manual_attempt_pending_) {
            const bool saved = saveCredentialsUnlocked(pending_ssid_, pending_password_);
            active_ssid_ = pending_ssid_;
            active_password_ = pending_password_;
            manual_attempt_pending_ = false;
            manual_result_ = saved ? WiFiManualResult::SUCCEEDED : WiFiManualResult::SAVE_FAILED;
            pending_ssid_ = "";
            pending_password_ = "";
            fallback_suppressed_ = false;
        }
        setState(WiFiState::CONNECTED);
        return;
    }

    if (state_ == WiFiState::CONNECTING) {
        if (now - attempt_started_at_ >= WIFI_CONNECT_TIMEOUT_MS) {
            if (manual_attempt_pending_) failManualAttempt(now);
            else {
                WiFi.disconnect(false, false);
                setState(WiFiState::ERROR);
                next_attempt_at_ = active_ssid_.length() == 0 ? UINT32_MAX : now + WIFI_RECONNECT_INTERVAL_MS;
            }
        }
        return;
    }

    if (state_ == WiFiState::CONNECTED) {
        ssid_[0] = '\0';
        ip_[0] = '\0';
        rssi_ = 0;
        setState(WiFiState::DISCONNECTED);
        next_attempt_at_ = now + WIFI_RECONNECT_INTERVAL_MS;
    }

    if (active_ssid_.length() > 0 && static_cast<int32_t>(now - next_attempt_at_) >= 0) {
        startAutomaticAttempt(now);
    }
}

WiFiSnapshot WiFiManager::snapshot() const
{
    SemaphoreGuard guard(mutex_);
    WiFiSnapshot result = {};
    result.state = state_;
    result.connected = state_ == WiFiState::CONNECTED;
    strncpy(result.ssid, ssid_, sizeof(result.ssid) - 1);
    strncpy(result.ip, ip_, sizeof(result.ip) - 1);
    result.rssi = rssi_;
    return result;
}

WiFiState WiFiManager::state() const { SemaphoreGuard guard(mutex_); return state_; }
bool WiFiManager::isConnected() const { SemaphoreGuard guard(mutex_); return state_ == WiFiState::CONNECTED; }
