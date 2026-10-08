#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../activity_claim.h"

constexpr uint16_t STORAGE_SCHEMA_VERSION = 1;
constexpr size_t MOVEMENT_REASON_MAX_LENGTH = 95;
constexpr int64_t MAX_ACCOUNT_BALANCE = 1000000000000LL;
// Manual credits are capped to reduce accidental oversized entries while
// remaining far above the quick-add amounts offered by the UI.
constexpr int32_t MAX_SINGLE_CREDIT = 10000;

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

struct StudentAccount {
    uint16_t student_id;
    int64_t balance;
    uint16_t schema_version;
};

enum class AccountMovementResult : uint8_t {
    OK, ACCOUNT_NOT_FOUND, INSUFFICIENT_FUNDS, INVALID_AMOUNT,
    INVALID_TIME, MOVEMENT_WRITE_FAILED, ACCOUNT_WRITE_FAILED, RECOVERY_REQUIRED
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
    StorageResult getStudentAccount(uint16_t student_id, StudentAccount &out) const;
    StorageResult getMovementById(uint64_t id, MovementRecord &out) const;
    StorageResult createAccountIfMissing(uint16_t student_id, int64_t initial_balance,
                                         StudentAccount &out);
    AccountMovementResult applyAccountMovement(uint16_t student_id, int64_t amount,
                                               StoredMovementType type, const char *reason,
                                               StoredRecordOrigin origin,
                                               StudentAccount &updated, uint64_t *created_id = nullptr);
    AccountMovementResult applyActivityReward(ActivityClaim &claim, StudentAccount &updated);

private:
    StorageState state_ = StorageState::STORAGE_UNINITIALIZED;
    uint64_t next_movement_id_ = 1;
    size_t total_bytes_ = 0;
    size_t used_bytes_ = 0;
    bool self_test_initialized_ = false;
    bool self_test_finished_ = false;
    bool recovery_required_ = false;
    bool transaction_in_progress_ = false;
    bool recoverPendingAccountMovement();
    StorageResult appendAccountSnapshot(const StudentAccount &account);
    StorageResult appendReservedMovement(const MovementRecord &record);
    bool writePendingActivityTransaction(const ActivityClaim &claim, uint64_t movement_id,
                                         int64_t old_balance, int64_t new_balance,
                                         int64_t timestamp, const char *reason);
};

extern StorageManager storage_manager;

// Development hook is deliberately disabled; startup never writes fake records.
constexpr bool STORAGE_SELF_TEST = false;

// Keep disabled in normal firmware. Enabling it permits one guarded format of
// the expected LittleFS partition after a failed mount.
constexpr bool STORAGE_ALLOW_ONE_TIME_FORMAT = false;
