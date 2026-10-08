#pragma once

#include <vector>

#include "../activity_claim.h"

enum class ActivityClaimStorageResult : uint8_t {
    OK, NOT_READY, INVALID_ARGUMENT, TIME_UNSYNCED, ID_ERROR,
    IO_ERROR, CORRUPT_DATA, NOT_FOUND
};

class ActivityClaimStorageManager {
public:
    ActivityClaimStorageResult begin();
    ActivityClaimStorageResult readClaims(std::vector<ActivityClaim> &claims) const;
    ActivityClaimStorageResult getClaimsForActivity(uint64_t activity_id,
                                                     std::vector<ActivityClaim> &claims) const;
    ActivityClaimStorageResult hasClaimForPair(uint64_t activity_id, uint16_t student_id,
                                               bool &has_claim) const;
    ActivityClaimStorageResult hasClaimForActivity(uint64_t activity_id,
                                                   bool &has_claim) const;
    ActivityClaimStorageResult reserveClaimId(uint64_t &id);
    ActivityClaimStorageResult recordMissingAccountIssue(const ActivityAccountIssue &issue);
    ActivityClaimStorageResult ensureClaimPersisted(const ActivityClaim &claim);

private:
    bool ready_ = false;
    uint64_t next_id_ = 1;
};

extern ActivityClaimStorageManager activity_claim_storage_manager;
