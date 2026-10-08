#pragma once

#include <stdint.h>
#include <vector>

constexpr uint16_t ACTIVITY_CLAIM_SCHEMA_VERSION = 1;

enum class ActivityClaimStatus : uint8_t { PAID, VOIDED };

struct ActivityClaim {
    uint64_t id = 0;
    uint16_t schema_version = ACTIVITY_CLAIM_SCHEMA_VERSION;
    uint64_t activity_id = 0;
    uint16_t student_id = 0;
    uint32_t reward_amount = 0;
    int64_t claimed_at = 0;
    uint64_t movement_id = 0;
    ActivityClaimStatus status = ActivityClaimStatus::PAID;
    uint64_t void_movement_id = 0;
};

struct ActivityAccountIssue {
    uint64_t activity_id = 0;
    uint16_t student_id = 0;
    int64_t occurred_at = 0;
    char reason[32] = "account_missing";
};
