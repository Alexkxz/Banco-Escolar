#pragma once

#include <stdint.h>
#include <time.h>

enum class TimeSyncState : uint8_t {
    TIME_UNSYNCED,
    TIME_SYNCING,
    TIME_SYNCED,
    TIME_ERROR
};

class TimeManager {
public:
    void begin();
    void update(bool wifi_connected);
    TimeSyncState state() const;
    bool isSynchronized() const;
    bool getLocalTime(struct tm &local_time) const;

private:
    void startSynchronization(uint32_t now);
    void logSynchronizedTime(time_t epoch) const;

    TimeSyncState state_ = TimeSyncState::TIME_UNSYNCED;
    bool previous_wifi_connected_ = false;
    uint32_t sync_started_at_ = 0;
    uint32_t next_sync_check_at_ = 0;
    uint32_t next_retry_at_ = 0;
};

extern TimeManager time_manager;
