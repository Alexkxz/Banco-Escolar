#include "activity_session.h"

#include <algorithm>
#include <stddef.h>

namespace {
bool has_duplicate_or_zero(const std::vector<uint16_t> &ids)
{
    for (size_t i = 0; i < ids.size(); ++i) {
        if (ids[i] == 0 || std::find(ids.begin(), ids.begin() + i, ids[i]) != ids.begin() + i) {
            return true;
        }
    }
    return false;
}
}

ActivityValidationResult validateActivityConfiguration(
    const ActivityConfiguration &configuration,
    const std::vector<uint16_t> &registered_student_ids)
{
    if (configuration.activity_number_enabled &&
        (configuration.activity_number == 0 || configuration.activity_number > MAX_ACTIVITY_NUMBER)) {
        return ActivityValidationResult::INVALID_NUMBER;
    }
    if (configuration.reward_amount == 0 || configuration.reward_amount > MAX_ACTIVITY_REWARD) {
        return ActivityValidationResult::INVALID_REWARD;
    }
    if ((configuration.timed &&
         (configuration.duration_seconds == 0 ||
          configuration.duration_seconds > MAX_ACTIVITY_DURATION_SECONDS)) ||
        (!configuration.timed && configuration.duration_seconds != 0)) {
        return ActivityValidationResult::INVALID_DURATION;
    }
    if (configuration.participant_mode != ActivityParticipantMode::ALL &&
        configuration.participant_mode != ActivityParticipantMode::SELECTED &&
        configuration.participant_mode != ActivityParticipantMode::PARTICIPANTS_DISABLED) {
        return ActivityValidationResult::INVALID_PARTICIPANT_MODE;
    }

    if (configuration.participant_mode == ActivityParticipantMode::PARTICIPANTS_DISABLED) {
        return ActivityValidationResult::VALID;
    }
    if (registered_student_ids.empty() || has_duplicate_or_zero(registered_student_ids)) {
        return ActivityValidationResult::INVALID_PARTICIPANTS;
    }
    if (configuration.participant_mode == ActivityParticipantMode::ALL) {
        return ActivityValidationResult::VALID;
    }
    if (configuration.selected_student_ids.empty()) {
        return ActivityValidationResult::INVALID_PARTICIPANTS;
    }
    if (has_duplicate_or_zero(configuration.selected_student_ids)) {
        return ActivityValidationResult::DUPLICATE_PARTICIPANT;
    }
    for (const uint16_t selected_id : configuration.selected_student_ids) {
        if (std::find(registered_student_ids.begin(), registered_student_ids.end(), selected_id) ==
            registered_student_ids.end()) {
            return ActivityValidationResult::INVALID_PARTICIPANTS;
        }
    }
    return ActivityValidationResult::VALID;
}

ActivityRemainingTime getActivityRemainingTime(const ActivitySession &session,
                                               int64_t now_epoch,
                                               bool now_is_valid)
{
    ActivityRemainingTime result;
    if (session.status != ActivitySessionStatus::ACTIVE) {
        result.state = ActivityRemainingState::CLOSED;
        return result;
    }
    if (session.duration_seconds == 0) {
        result.state = ActivityRemainingState::UNTIMED;
        return result;
    }
    if (!now_is_valid || session.started_at < MIN_VALID_ACTIVITY_EPOCH ||
        now_epoch < MIN_VALID_ACTIVITY_EPOCH || now_epoch < session.started_at) {
        result.state = ActivityRemainingState::PENDING_VALID_TIME;
        return result;
    }

    const uint64_t elapsed = static_cast<uint64_t>(now_epoch - session.started_at);
    if (elapsed >= session.duration_seconds) {
        result.state = ActivityRemainingState::EXPIRED;
        return result;
    }
    result.state = ActivityRemainingState::RUNNING;
    result.seconds = session.duration_seconds - static_cast<uint32_t>(elapsed);
    return result;
}

ActivityEligibility getActivityEligibility(const ActivitySession &session,
                                          int64_t now_epoch, bool now_is_valid)
{
    if (session.status != ActivitySessionStatus::ACTIVE) return ActivityEligibility::CLOSED;
    const ActivityRemainingTime remaining = getActivityRemainingTime(session, now_epoch, now_is_valid);
    switch (remaining.state) {
        case ActivityRemainingState::UNTIMED:
        case ActivityRemainingState::RUNNING: return ActivityEligibility::ELIGIBLE;
        case ActivityRemainingState::EXPIRED: return ActivityEligibility::EXPIRED;
        case ActivityRemainingState::PENDING_VALID_TIME: return ActivityEligibility::TIME_UNVERIFIED;
        case ActivityRemainingState::CLOSED: return ActivityEligibility::CLOSED;
    }
    return ActivityEligibility::TIME_UNVERIFIED;
}

bool canEditActivityBeforeFirstClaim(const ActivitySession &session, bool has_prior_claims)
{
    return session.status == ActivitySessionStatus::ACTIVE && !has_prior_claims;
}
