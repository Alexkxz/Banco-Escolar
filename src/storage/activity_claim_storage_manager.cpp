#include "activity_claim_storage_manager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>

#include <algorithm>
#include <cstring>
#include <limits>

#include "activity_session.h"
#include "storage_manager.h"

namespace {
constexpr char CLAIMS_FILE[] = "/data/activity_claims.ndjson";
constexpr char DATA_DIRECTORY[] = "/data";
constexpr char ID_NAMESPACE[] = "bank_storage";
constexpr char NEXT_CLAIM_ID_KEY[] = "next_claim_id";
constexpr size_t MAX_CLAIM_LINE_LENGTH = 384;

const char *claimStatusName(ActivityClaimStatus status)
{
    return status == ActivityClaimStatus::PAID ? "paid" : "voided";
}

bool parseClaimLine(const char *line, ActivityClaim &claim)
{
    StaticJsonDocument<384> document;
    if (deserializeJson(document, line) != DeserializationError::Ok ||
        (document["schema_version"] | 0) != ACTIVITY_CLAIM_SCHEMA_VERSION ||
        strcmp(document["record"] | "", "activity_claim") != 0 ||
        !document["id"].is<uint64_t>() || !document["activity_id"].is<uint64_t>() ||
        !document["student_id"].is<uint16_t>() || !document["reward_amount"].is<uint32_t>() ||
        !document["claimed_at"].is<int64_t>() || !document["movement_id"].is<uint64_t>()) return false;
    const char *status = document["status"] | "";
    if (strcmp(status, "paid") == 0) claim.status = ActivityClaimStatus::PAID;
    else if (strcmp(status, "voided") == 0) claim.status = ActivityClaimStatus::VOIDED;
    else return false;
    claim.id = document["id"].as<uint64_t>();
    claim.schema_version = ACTIVITY_CLAIM_SCHEMA_VERSION;
    claim.activity_id = document["activity_id"].as<uint64_t>();
    claim.student_id = document["student_id"].as<uint16_t>();
    claim.reward_amount = document["reward_amount"].as<uint32_t>();
    claim.claimed_at = document["claimed_at"].as<int64_t>();
    claim.movement_id = document["movement_id"].as<uint64_t>();
    claim.void_movement_id = document["void_movement_id"] | static_cast<uint64_t>(0);
    return claim.id != 0 && claim.activity_id != 0 && claim.student_id != 0 &&
           claim.reward_amount != 0 && claim.claimed_at >= MIN_VALID_ACTIVITY_EPOCH &&
           claim.movement_id != 0 &&
           (claim.status == ActivityClaimStatus::PAID ? claim.void_movement_id == 0 : claim.void_movement_id != 0);
}

bool parseIssueLine(const char *line, ActivityAccountIssue &issue)
{
    StaticJsonDocument<256> document;
    if (deserializeJson(document, line) != DeserializationError::Ok ||
        (document["schema_version"] | 0) != ACTIVITY_CLAIM_SCHEMA_VERSION ||
        strcmp(document["record"] | "", "account_issue") != 0 ||
        !document["activity_id"].is<uint64_t>() || !document["student_id"].is<uint16_t>() ||
        !document["occurred_at"].is<int64_t>()) return false;
    const char *reason = document["reason"] | "";
    if (strcmp(reason, "account_missing") != 0) return false;
    issue.activity_id = document["activity_id"].as<uint64_t>();
    issue.student_id = document["student_id"].as<uint16_t>();
    issue.occurred_at = document["occurred_at"].as<int64_t>();
    strcpy(issue.reason, reason);
    return issue.activity_id != 0 && issue.student_id != 0 &&
           issue.occurred_at >= MIN_VALID_ACTIVITY_EPOCH;
}

bool sameImmutableClaimFields(const ActivityClaim &a, const ActivityClaim &b)
{
    return a.id == b.id && a.activity_id == b.activity_id && a.student_id == b.student_id &&
           a.reward_amount == b.reward_amount && a.claimed_at == b.claimed_at &&
           a.movement_id == b.movement_id;
}

bool readAllRecords(std::vector<ActivityClaim> &claims,
                    std::vector<ActivityAccountIssue> &issues)
{
    claims.clear();
    issues.clear();
    if (!LittleFS.exists(CLAIMS_FILE)) return true;
    File file = LittleFS.open(CLAIMS_FILE, FILE_READ);
    if (!file) return false;
    char line[MAX_CLAIM_LINE_LENGTH];
    while (file.available()) {
        size_t length = 0;
        bool overflow = false;
        bool terminated = false;
        while (file.available()) {
            const int value = file.read();
            if (value < 0) break;
            if (value == '\n') { terminated = true; break; }
            if (length + 1U < sizeof(line)) line[length++] = static_cast<char>(value);
            else overflow = true;
        }
        if (!terminated || overflow || length == 0) { file.close(); return false; }
        if (length > 0 && line[length - 1] == '\r') --length;
        line[length] = '\0';
        StaticJsonDocument<384> doc;
        if (deserializeJson(doc, line) != DeserializationError::Ok) { file.close(); return false; }
        const char *record_type = doc["record"] | "";
        if (strcmp(record_type, "activity_claim") == 0) {
            ActivityClaim parsed;
            if (!parseClaimLine(line, parsed)) { file.close(); return false; }
            MovementRecord movement = {};
            char expected_reason[MOVEMENT_REASON_MAX_LENGTH + 1] = {};
            snprintf(expected_reason, sizeof(expected_reason), "Actividad %llu",
                     static_cast<unsigned long long>(parsed.activity_id));
            if (storage_manager.getMovementById(parsed.movement_id, movement) != StorageResult::STORAGE_OK ||
                movement.student_id != parsed.student_id || movement.amount != static_cast<int32_t>(parsed.reward_amount) ||
                movement.type != StoredMovementType::ENTRY || movement.timestamp != parsed.claimed_at ||
                movement.origin != StoredRecordOrigin::TERMINAL || movement.synced ||
                strcmp(movement.reason, expected_reason) != 0) { file.close(); return false; }
            const auto existing = std::find_if(claims.begin(), claims.end(), [&parsed](const ActivityClaim &c) {
                return c.id == parsed.id;
            });
            if (existing == claims.end()) claims.push_back(parsed);
            else {
                if (!sameImmutableClaimFields(*existing, parsed)) { file.close(); return false; }
                *existing = parsed;
            }
            for (const ActivityClaim &other : claims) {
                if (other.activity_id == parsed.activity_id && other.student_id == parsed.student_id &&
                    other.id != parsed.id) { file.close(); return false; }
            }
        } else if (strcmp(record_type, "account_issue") == 0) {
            ActivityAccountIssue parsed;
            if (!parseIssueLine(line, parsed)) { file.close(); return false; }
            const bool duplicate = std::any_of(issues.begin(), issues.end(), [&parsed](const ActivityAccountIssue &i) {
                return i.activity_id == parsed.activity_id && i.student_id == parsed.student_id;
            });
            if (duplicate) { file.close(); return false; }
            issues.push_back(parsed);
        } else { file.close(); return false; }
    }
    file.close();
    return true;
}

bool appendDocument(JsonDocument &document)
{
    if (!LittleFS.exists(DATA_DIRECTORY) && !LittleFS.mkdir(DATA_DIRECTORY)) return false;
    const size_t expected = measureJson(document);
    if (expected + 1U >= MAX_CLAIM_LINE_LENGTH) return false;
    File file = LittleFS.open(CLAIMS_FILE, FILE_APPEND);
    if (!file) return false;
    const size_t written = serializeJson(document, file);
    const bool newline_written = file.write(static_cast<uint8_t>('\n')) == 1;
    file.flush();
    file.close();
    return written == expected && newline_written;
}

bool appendClaim(const ActivityClaim &claim)
{
    StaticJsonDocument<384> document;
    document["schema_version"] = ACTIVITY_CLAIM_SCHEMA_VERSION;
    document["record"] = "activity_claim";
    document["id"] = claim.id;
    document["activity_id"] = claim.activity_id;
    document["student_id"] = claim.student_id;
    document["reward_amount"] = claim.reward_amount;
    document["claimed_at"] = claim.claimed_at;
    document["movement_id"] = claim.movement_id;
    document["status"] = claimStatusName(claim.status);
    if (claim.void_movement_id) document["void_movement_id"] = claim.void_movement_id;
    return appendDocument(document);
}

bool persistNextClaimId(uint64_t next_id)
{
    Preferences preferences;
    if (!preferences.begin(ID_NAMESPACE, false)) return false;
    const size_t written = preferences.putULong64(NEXT_CLAIM_ID_KEY, next_id);
    preferences.end();
    return written == sizeof(uint64_t);
}
}

ActivityClaimStorageManager activity_claim_storage_manager;

ActivityClaimStorageResult ActivityClaimStorageManager::begin()
{
    ready_ = false;
    std::vector<ActivityClaim> claims;
    std::vector<ActivityAccountIssue> issues;
    if (!readAllRecords(claims, issues)) return ActivityClaimStorageResult::CORRUPT_DATA;
    uint64_t maximum_id = 0;
    for (const ActivityClaim &claim : claims) maximum_id = std::max(maximum_id, claim.id);
    if (maximum_id == std::numeric_limits<uint64_t>::max()) return ActivityClaimStorageResult::ID_ERROR;
    Preferences preferences;
    if (!preferences.begin(ID_NAMESPACE, true)) return ActivityClaimStorageResult::IO_ERROR;
    const uint64_t reserved = preferences.getULong64(NEXT_CLAIM_ID_KEY, 0);
    preferences.end();
    next_id_ = std::max<uint64_t>(1, std::max(reserved, maximum_id + 1));
    ready_ = true;
    return ActivityClaimStorageResult::OK;
}

ActivityClaimStorageResult ActivityClaimStorageManager::readClaims(std::vector<ActivityClaim> &claims) const
{
    if (!ready_ || !storage_manager.isReady()) return ActivityClaimStorageResult::NOT_READY;
    std::vector<ActivityAccountIssue> issues;
    return readAllRecords(claims, issues) ? ActivityClaimStorageResult::OK
                                          : ActivityClaimStorageResult::CORRUPT_DATA;
}

ActivityClaimStorageResult ActivityClaimStorageManager::getClaimsForActivity(
    uint64_t activity_id, std::vector<ActivityClaim> &claims) const
{
    if (!activity_id) return ActivityClaimStorageResult::INVALID_ARGUMENT;
    std::vector<ActivityClaim> all;
    const ActivityClaimStorageResult result = readClaims(all);
    if (result != ActivityClaimStorageResult::OK) return result;
    claims.clear();
    for (const ActivityClaim &claim : all) if (claim.activity_id == activity_id) claims.push_back(claim);
    return ActivityClaimStorageResult::OK;
}

ActivityClaimStorageResult ActivityClaimStorageManager::hasClaimForPair(
    uint64_t activity_id, uint16_t student_id, bool &has_claim) const
{
    has_claim = false;
    std::vector<ActivityClaim> all;
    const ActivityClaimStorageResult result = readClaims(all);
    if (result != ActivityClaimStorageResult::OK) return result;
    for (const ActivityClaim &claim : all) {
        if (claim.activity_id == activity_id && claim.student_id == student_id) {
            has_claim = true; // Includes VOIDED: recobro needs its own future authorization phase.
            break;
        }
    }
    return ActivityClaimStorageResult::OK;
}

ActivityClaimStorageResult ActivityClaimStorageManager::hasClaimForActivity(
    uint64_t activity_id, bool &has_claim) const
{
    has_claim = false;
    std::vector<ActivityClaim> all;
    const ActivityClaimStorageResult result = readClaims(all);
    if (result != ActivityClaimStorageResult::OK) return result;
    for (const ActivityClaim &claim : all) if (claim.activity_id == activity_id) { has_claim = true; break; }
    return ActivityClaimStorageResult::OK;
}

ActivityClaimStorageResult ActivityClaimStorageManager::reserveClaimId(uint64_t &id)
{
    if (!ready_ || !storage_manager.isReady()) return ActivityClaimStorageResult::NOT_READY;
    if (next_id_ == 0 || next_id_ == std::numeric_limits<uint64_t>::max()) return ActivityClaimStorageResult::ID_ERROR;
    id = next_id_;
    if (!persistNextClaimId(id + 1)) return ActivityClaimStorageResult::IO_ERROR;
    ++next_id_;
    return ActivityClaimStorageResult::OK;
}

ActivityClaimStorageResult ActivityClaimStorageManager::recordMissingAccountIssue(
    const ActivityAccountIssue &issue)
{
    if (!ready_ || !storage_manager.isReady()) return ActivityClaimStorageResult::NOT_READY;
    if (!issue.activity_id || !issue.student_id || issue.occurred_at < MIN_VALID_ACTIVITY_EPOCH ||
        strcmp(issue.reason, "account_missing") != 0) return ActivityClaimStorageResult::INVALID_ARGUMENT;
    std::vector<ActivityClaim> claims;
    std::vector<ActivityAccountIssue> issues;
    if (!readAllRecords(claims, issues)) return ActivityClaimStorageResult::CORRUPT_DATA;
    for (const ActivityAccountIssue &existing : issues) {
        if (existing.activity_id == issue.activity_id && existing.student_id == issue.student_id)
            return ActivityClaimStorageResult::OK;
    }
    StaticJsonDocument<256> document;
    document["schema_version"] = ACTIVITY_CLAIM_SCHEMA_VERSION;
    document["record"] = "account_issue";
    document["activity_id"] = issue.activity_id;
    document["student_id"] = issue.student_id;
    document["occurred_at"] = issue.occurred_at;
    document["reason"] = issue.reason;
    return appendDocument(document) ? ActivityClaimStorageResult::OK : ActivityClaimStorageResult::IO_ERROR;
}

ActivityClaimStorageResult ActivityClaimStorageManager::ensureClaimPersisted(const ActivityClaim &claim)
{
    if (!claim.id || !claim.activity_id || !claim.student_id || !claim.reward_amount ||
        claim.claimed_at < MIN_VALID_ACTIVITY_EPOCH || !claim.movement_id ||
        claim.status != ActivityClaimStatus::PAID || claim.void_movement_id) return ActivityClaimStorageResult::INVALID_ARGUMENT;
    std::vector<ActivityClaim> claims;
    std::vector<ActivityAccountIssue> issues;
    if (!readAllRecords(claims, issues)) return ActivityClaimStorageResult::CORRUPT_DATA;
    bool same_id_found = false;
    for (const ActivityClaim &existing : claims) {
        if (existing.activity_id == claim.activity_id && existing.student_id == claim.student_id) {
            if (existing.id != claim.id || !sameImmutableClaimFields(existing, claim))
                return ActivityClaimStorageResult::CORRUPT_DATA;
            same_id_found = true;
        }
    }
    if (same_id_found) return ActivityClaimStorageResult::OK;
    for (const ActivityClaim &existing : claims) if (existing.id == claim.id) return ActivityClaimStorageResult::CORRUPT_DATA;
    return appendClaim(claim) ? ActivityClaimStorageResult::OK : ActivityClaimStorageResult::IO_ERROR;
}
