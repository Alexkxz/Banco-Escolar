#pragma once

#include <stdint.h>
#include <vector>

constexpr uint16_t ACTIVITY_SESSION_SCHEMA_VERSION = 1;
constexpr uint16_t MAX_ACTIVITY_NUMBER = 9999;
constexpr uint32_t MAX_ACTIVITY_REWARD = 10000;
constexpr uint32_t MAX_ACTIVITY_DURATION_SECONDS = 999U * 60U + 59U;
constexpr int64_t MIN_VALID_ACTIVITY_EPOCH = 1735689600LL;

enum class ActivityParticipantMode : uint8_t {
    ALL,
    SELECTED,
    PARTICIPANTS_DISABLED
};

enum class ActivitySessionStatus : uint8_t {
    ACTIVE,
    FINISHED,
    CANCELLED
};

// UI-independent copy of the values collected by ActivityDraft.
struct ActivityConfiguration {
    bool activity_number_enabled = false;
    uint16_t activity_number = 0;
    bool timed = false;
    uint32_t duration_seconds = 0;
    uint32_t reward_amount = 0;
    ActivityParticipantMode participant_mode = ActivityParticipantMode::ALL;
    std::vector<uint16_t> selected_student_ids;
};

struct ActivitySession {
    uint64_t id = 0;
    uint16_t schema_version = ACTIVITY_SESSION_SCHEMA_VERSION;
    bool activity_number_enabled = false;
    uint16_t activity_number = 0;
    uint32_t reward_amount = 0;
    uint32_t duration_seconds = 0;
    ActivityParticipantMode participant_mode = ActivityParticipantMode::ALL;
    // Snapshot of eligible participants. ALL captures the roster at start;
    // PARTICIPANTS_DISABLED deliberately stores an empty list.
    std::vector<uint16_t> participant_student_ids;
    int64_t started_at = 0;
    ActivitySessionStatus status = ActivitySessionStatus::ACTIVE;
    int64_t closed_at = 0; // Zero means no close timestamp.
};

enum class ActivityValidationResult : uint8_t {
    VALID,
    INVALID_NUMBER,
    INVALID_REWARD,
    INVALID_DURATION,
    INVALID_PARTICIPANT_MODE,
    INVALID_PARTICIPANTS,
    DUPLICATE_PARTICIPANT
};

enum class ActivityRemainingState : uint8_t {
    UNTIMED,
    PENDING_VALID_TIME,
    RUNNING,
    EXPIRED,
    CLOSED
};

struct ActivityRemainingTime {
    ActivityRemainingState state = ActivityRemainingState::PENDING_VALID_TIME;
    uint32_t seconds = 0;
};

enum class ActivityEligibility : uint8_t {
    ELIGIBLE, CLOSED, EXPIRED, TIME_UNVERIFIED
};

ActivityEligibility getActivityEligibility(const ActivitySession &session,
                                          int64_t now_epoch, bool now_is_valid);
// Caller supplies the real ActivityClaim history result. Any prior claim,
// including VOIDED, keeps the session non-editable under the current policy.
bool canEditActivityBeforeFirstClaim(const ActivitySession &session,
                                     bool has_prior_claims);

ActivityValidationResult validateActivityConfiguration(
    const ActivityConfiguration &configuration,
    const std::vector<uint16_t> &registered_student_ids);

ActivityRemainingTime getActivityRemainingTime(const ActivitySession &session,
                                               int64_t now_epoch,
                                               bool now_is_valid);
