#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr uint16_t STORAGE_SCHEMA_VERSION = 1;
constexpr size_t MOVEMENT_REASON_MAX_LENGTH = 95;

enum class StorageState : uint8_t {
    STORAGE_UNINITIALIZED,
    STORAGE_READY,
    STORAGE_ERROR,
    STORAGE_UNAVAILABLE
};

enum class StorageResult : uint8_t {
    STORAGE_OK,
    STORAGE_NOT_READY,
    STORAGE_INVALID_ARGUMENT,
    STORAGE_TIME_UNSYNCED,
    STORAGE_ID_ERROR,
    STORAGE_IO_ERROR,
    STORAGE_NOT_FOUND,
    STORAGE_UNSUPPORTED
};

enum class StoredMovementType : uint8_t {
    ENTRY,
    EXIT
};

enum class StoredRecordOrigin : uint8_t {
    TERMINAL,
    PANEL
};

enum class AttendanceType : uint8_t {
    CHECK_IN,
    CHECK_OUT
};

struct MovementRecord {
    uint64_t id;
    uint16_t student_id;
    int32_t amount;
    StoredMovementType type;
    char reason[MOVEMENT_REASON_MAX_LENGTH + 1];
    int64_t timestamp; // Unix seconds
    StoredRecordOrigin origin;
    bool synced;
    uint16_t schema_version;
};

struct StoredAttendanceRecord {
    uint64_t id;
    uint16_t student_id;
    int64_t timestamp; // Unix seconds
    AttendanceType type;
    StoredRecordOrigin origin;
    bool synced;
    uint16_t schema_version;
};

class StorageManager {
public:
    bool begin();
    void updateSelfTest();

    StorageState state() const;
    bool isReady() const;
    size_t totalBytes() const;
    size_t usedBytes() const;
    size_t freeBytes() const;
    size_t getTotalBytes() const;
    size_t getUsedBytes() const;
    size_t getFreeBytes() const;
    uint64_t nextMovementId() const;

    // Deletes only known local data files; preserves NVS and the ID counter.
    StorageResult clearLocalData();

    StorageResult appendMovement(uint16_t student_id, int32_t amount,
                                 StoredMovementType type, const char *reason,
                                 StoredRecordOrigin origin = StoredRecordOrigin::TERMINAL,
                                 uint64_t *created_id = nullptr);
    size_t getMovementCount() const;
    size_t getPendingMovementCount() const;
    StorageResult readMovements(size_t offset, MovementRecord *records,
                                size_t capacity, size_t &read_count) const;
    StorageResult getPendingMovements(size_t offset, MovementRecord *records,
                                      size_t capacity, size_t &read_count) const;

    // Append-only acknowledgement events are intentionally deferred until a
    // synchronization phase defines the durable server acknowledgement flow.
    StorageResult markMovementSynced(uint64_t id);

private:
    StorageState state_ = StorageState::STORAGE_UNINITIALIZED;
    uint64_t next_movement_id_ = 1;
    size_t total_bytes_ = 0;
    size_t used_bytes_ = 0;
    bool self_test_initialized_ = false;
    bool self_test_finished_ = false;
};

extern StorageManager storage_manager;

// Development hook is deliberately disabled; startup never writes fake records.
constexpr bool STORAGE_SELF_TEST = false;

// Keep disabled in normal firmware. Enabling it permits one guarded format of
// the expected LittleFS partition after a failed mount.
constexpr bool STORAGE_ALLOW_ONE_TIME_FORMAT = false;
