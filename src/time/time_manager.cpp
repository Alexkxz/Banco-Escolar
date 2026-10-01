#include "time_manager.h"

#include <Arduino.h>
#include <cstdlib>
#include <sys/time.h>

namespace {
// Zona geográfica de referencia del proyecto: America/Mexico_City.
// configTzTime() pasa esta cadena a setenv("TZ", ...) y tzset(); el
// framework/libc instalado interpreta reglas TZ POSIX y no carga una base
// IANA/tzdata. La regla POSIX actual para Ciudad de México es CST6.
// Si cambia la legislación, actualizar únicamente TIME_ZONE_POSIX.
constexpr char TIME_ZONE_IANA[] = "America/Mexico_City";
constexpr char TIME_ZONE_POSIX[] = "CST6";
constexpr char NTP_SERVER_PRIMARY[] = "pool.ntp.org";
constexpr char NTP_SERVER_SECONDARY[] = "time.nist.gov";
constexpr time_t MIN_VALID_EPOCH = 1735689600LL; // 2025-01-01 00:00:00 UTC
constexpr uint32_t SYNC_CHECK_INTERVAL_MS = 1000;
constexpr uint32_t SYNC_TIMEOUT_MS = 60000;
constexpr uint32_t SYNC_RETRY_INTERVAL_MS = 60000;
}

TimeManager time_manager;

void TimeManager::begin()
{
    setenv("TZ", TIME_ZONE_POSIX, 1);
    tzset();
    (void)TIME_ZONE_IANA;
    state_ = TimeSyncState::TIME_UNSYNCED;
    previous_wifi_connected_ = false;
    Serial.println("[Time] Esperando Wi-Fi");
}

void TimeManager::startSynchronization(uint32_t now)
{
    configTzTime(TIME_ZONE_POSIX, NTP_SERVER_PRIMARY, NTP_SERVER_SECONDARY);
    state_ = TimeSyncState::TIME_SYNCING;
    sync_started_at_ = now;
    next_sync_check_at_ = now;
    Serial.println("[Time] Sincronizando NTP");
}

void TimeManager::logSynchronizedTime(time_t epoch) const
{
    struct tm local_time = {};
    if (localtime_r(&epoch, &local_time) == nullptr) return;

    int hour_12 = local_time.tm_hour % 12;
    if (hour_12 == 0) hour_12 = 12;
    const char *period = local_time.tm_hour < 12 ? "a.m." : "p.m.";
    Serial.printf("[Time] Hora sincronizada\n");
    Serial.printf("[Time] Fecha/hora: %02d/%02d/%04d %d:%02d:%02d %s\n",
                  local_time.tm_mday, local_time.tm_mon + 1, local_time.tm_year + 1900,
                  hour_12, local_time.tm_min, local_time.tm_sec, period);
}

void TimeManager::update(bool wifi_connected)
{
    const uint32_t now = millis();
    if (!wifi_connected) {
        previous_wifi_connected_ = false;
        return;
    }

    if (!previous_wifi_connected_) {
        previous_wifi_connected_ = true;
        startSynchronization(now);
    }

    if (state_ == TimeSyncState::TIME_SYNCED || state_ == TimeSyncState::TIME_UNSYNCED) return;
    if (state_ == TimeSyncState::TIME_ERROR) {
        if (static_cast<int32_t>(now - next_retry_at_) >= 0) startSynchronization(now);
        return;
    }

    if (static_cast<int32_t>(now - next_sync_check_at_) < 0) return;
    next_sync_check_at_ = now + SYNC_CHECK_INTERVAL_MS;

    time_t epoch = 0;
    time(&epoch);
    if (epoch >= MIN_VALID_EPOCH) {
        state_ = TimeSyncState::TIME_SYNCED;
        logSynchronizedTime(epoch);
    } else if (now - sync_started_at_ >= SYNC_TIMEOUT_MS) {
        state_ = TimeSyncState::TIME_ERROR;
        next_retry_at_ = now + SYNC_RETRY_INTERVAL_MS;
        Serial.println("[Time] Error de sincronizacion NTP; reintentando en 60 s");
    }
}

TimeSyncState TimeManager::state() const
{
    return state_;
}

bool TimeManager::isSynchronized() const
{
    return state_ == TimeSyncState::TIME_SYNCED;
}

bool TimeManager::getLocalTime(struct tm &local_time) const
{
    time_t epoch = 0;
    time(&epoch);
    if (epoch < MIN_VALID_EPOCH) return false;
    return localtime_r(&epoch, &local_time) != nullptr;
}
