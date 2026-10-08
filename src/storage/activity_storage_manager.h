#pragma once

#include <stdint.h>
#include <vector>

#include "activity_session.h"

enum class ActivityStorageResult : uint8_t {
    OK,
    NOT_READY,
    INVALID_ARGUMENT,
    TIME_UNSYNCED,
    ID_ERROR,
    IO_ERROR,
    CORRUPT_DATA,
    INCOMPLETE_TAIL,
    NOT_FOUND,
    INVALID_STATE
};

class ActivityStorageManager {
public:
    ActivityStorageResult begin();

    ActivityStorageResult createSession(const ActivityConfiguration &configuration,
                                        const std::vector<uint16_t> &registered_student_ids,
                                        int64_t started_at,
                                        ActivitySession &created);

    // Reads the latest valid append-only snapshot for every session ID.
    // Valid sessions are returned even when the result reports a damaged tail.
    ActivityStorageResult readSessions(std::vector<ActivitySession> &sessions) const;
    ActivityStorageResult readSession(uint64_t id, ActivitySession &session) const;

    // Status changes append a full new snapshot; prior records are retained.
    ActivityStorageResult closeSession(uint64_t id, ActivitySessionStatus status,
                                       int64_t closed_at, ActivitySession &updated);
    ActivityStorageResult updateActiveSession(uint64_t id,
                                             const ActivityConfiguration &configuration,
                                             const std::vector<uint16_t> &registered_student_ids,
                                             ActivitySession &updated);

private:
    ActivityStorageResult appendSnapshot(const ActivitySession &session);

    bool ready_ = false;
    uint64_t next_activity_id_ = 1;
};

extern ActivityStorageManager activity_storage_manager;
