#include "activity_storage_manager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>

#include <algorithm>
#include <cstring>
#include <limits>

#include "storage_manager.h"
#include "activity_claim_storage_manager.h"

namespace {
constexpr char ACTIVITIES_DIRECTORY[] = "/data";
constexpr char ACTIVITIES_FILE[] = "/data/activities.ndjson";
constexpr char ID_NAMESPACE[] = "bank_storage";
constexpr char NEXT_ACTIVITY_ID_KEY[] = "next_act_id";
constexpr size_t MAX_ACTIVITY_LINE_LENGTH = 1024;

const char *participantModeName(ActivityParticipantMode mode)
{
    switch (mode) {
        case ActivityParticipantMode::ALL: return "all";
        case ActivityParticipantMode::SELECTED: return "selected";
        case ActivityParticipantMode::PARTICIPANTS_DISABLED: return "participants_disabled";
    }
    return "invalid";
}

const char *statusName(ActivitySessionStatus status)
{
    switch (status) {
        case ActivitySessionStatus::ACTIVE: return "active";
        case ActivitySessionStatus::FINISHED: return "finished";
        case ActivitySessionStatus::CANCELLED: return "cancelled";
    }
    return "invalid";
}

bool parseParticipantMode(const char *name, ActivityParticipantMode &mode)
{
    if (strcmp(name, "all") == 0) mode = ActivityParticipantMode::ALL;
    else if (strcmp(name, "selected") == 0) mode = ActivityParticipantMode::SELECTED;
    else if (strcmp(name, "participants_disabled") == 0) mode = ActivityParticipantMode::PARTICIPANTS_DISABLED;
    else return false;
    return true;
}

bool parseStatus(const char *name, ActivitySessionStatus &status)
{
    if (strcmp(name, "active") == 0) status = ActivitySessionStatus::ACTIVE;
    else if (strcmp(name, "finished") == 0) status = ActivitySessionStatus::FINISHED;
    else if (strcmp(name, "cancelled") == 0) status = ActivitySessionStatus::CANCELLED;
    else return false;
    return true;
}

bool readLine(File &file, char *line, size_t capacity, bool &terminated, bool &overflow)
{
    size_t length = 0;
    bool read_any = false;
    terminated = false;
    overflow = false;
    while (file.available()) {
        const int value = file.read();
        if (value < 0) break;
        read_any = true;
        if (value == '\n') {
            terminated = true;
            break;
        }
        if (length + 1 < capacity) line[length++] = static_cast<char>(value);
        else overflow = true;
    }
    if (length > 0 && line[length - 1] == '\r') --length;
    line[length] = '\0';
    return read_any;
}

bool parseSessionLine(const char *line, ActivitySession &session)
{
    StaticJsonDocument<1024> document;
    if (deserializeJson(document, line) != DeserializationError::Ok ||
        (document["schema_version"] | 0) != ACTIVITY_SESSION_SCHEMA_VERSION ||
        strcmp(document["record"] | "", "activity") != 0 ||
        !document["id"].is<uint64_t>() || !document["activity_number_enabled"].is<bool>() ||
        !document["activity_number"].is<uint16_t>() || !document["reward_amount"].is<uint32_t>() ||
        !document["duration_seconds"].is<uint32_t>() || !document["started_at"].is<int64_t>() ||
        !document["participant_student_ids"].is<JsonArrayConst>()) {
        return false;
    }

    const char *mode_name = document["participant_mode"] | "";
    const char *state_name = document["status"] | "";
    if (!parseParticipantMode(mode_name, session.participant_mode) ||
        !parseStatus(state_name, session.status)) return false;

    session.id = document["id"].as<uint64_t>();
    session.schema_version = ACTIVITY_SESSION_SCHEMA_VERSION;
    session.activity_number_enabled = document["activity_number_enabled"].as<bool>();
    session.activity_number = document["activity_number"].as<uint16_t>();
    session.reward_amount = document["reward_amount"].as<uint32_t>();
    session.duration_seconds = document["duration_seconds"].as<uint32_t>();
    session.started_at = document["started_at"].as<int64_t>();
    session.closed_at = document["closed_at"] | static_cast<int64_t>(0);

    session.participant_student_ids.clear();
    for (JsonVariantConst value : document["participant_student_ids"].as<JsonArrayConst>()) {
        if (!value.is<uint16_t>()) return false;
        session.participant_student_ids.push_back(value.as<uint16_t>());
    }

    if (session.id == 0 || session.started_at < MIN_VALID_ACTIVITY_EPOCH ||
        session.reward_amount == 0 || session.reward_amount > MAX_ACTIVITY_REWARD ||
        session.duration_seconds > MAX_ACTIVITY_DURATION_SECONDS ||
        (session.activity_number_enabled &&
         (session.activity_number == 0 || session.activity_number > MAX_ACTIVITY_NUMBER)) ||
        (session.status == ActivitySessionStatus::ACTIVE && session.closed_at != 0) ||
        (session.status != ActivitySessionStatus::ACTIVE &&
         (session.closed_at < session.started_at || session.closed_at < MIN_VALID_ACTIVITY_EPOCH))) {
        return false;
    }

    ActivityConfiguration configuration;
    configuration.activity_number_enabled = session.activity_number_enabled;
    configuration.activity_number = session.activity_number;
    configuration.timed = session.duration_seconds > 0;
    configuration.duration_seconds = session.duration_seconds;
    configuration.reward_amount = session.reward_amount;
    configuration.participant_mode = session.participant_mode;
    if (session.participant_mode == ActivityParticipantMode::SELECTED) {
        configuration.selected_student_ids = session.participant_student_ids;
    }
    if (validateActivityConfiguration(configuration, session.participant_student_ids) !=
        ActivityValidationResult::VALID) return false;
    if (session.participant_mode == ActivityParticipantMode::PARTICIPANTS_DISABLED &&
        !session.participant_student_ids.empty()) return false;
    return true;
}

bool readLatestSessions(std::vector<ActivitySession> &sessions,
                        ActivityStorageResult &read_result,
                        uint64_t &maximum_id)
{
    sessions.clear();
    maximum_id = 0;
    read_result = ActivityStorageResult::OK;
    if (!LittleFS.exists(ACTIVITIES_FILE)) return true;
    File file = LittleFS.open(ACTIVITIES_FILE, FILE_READ);
    if (!file) {
        read_result = ActivityStorageResult::IO_ERROR;
        return false;
    }

    char line[MAX_ACTIVITY_LINE_LENGTH];
    bool terminated = false;
    bool overflow = false;
    while (readLine(file, line, sizeof(line), terminated, overflow)) {
        if (!terminated) {
            read_result = ActivityStorageResult::INCOMPLETE_TAIL;
            break;
        }
        ActivitySession parsed;
        if (overflow || !parseSessionLine(line, parsed)) {
            // Retain all data and surface the damaged record. Appends are
            // blocked until the file has been inspected/recovered explicitly.
            read_result = ActivityStorageResult::CORRUPT_DATA;
            continue;
        }
        maximum_id = std::max(maximum_id, parsed.id);
        const auto existing = std::find_if(sessions.begin(), sessions.end(),
            [&parsed](const ActivitySession &item) { return item.id == parsed.id; });
        if (existing == sessions.end()) sessions.push_back(parsed);
        else *existing = parsed;
    }
    file.close();
    return read_result != ActivityStorageResult::IO_ERROR;
}

bool reserveNextActivityId(uint64_t next_id)
{
    Preferences preferences;
    if (!preferences.begin(ID_NAMESPACE, false)) return false;
    const size_t written = preferences.putULong64(NEXT_ACTIVITY_ID_KEY, next_id);
    preferences.end();
    return written == sizeof(uint64_t);
}
}

ActivityStorageManager activity_storage_manager;

ActivityStorageResult ActivityStorageManager::begin()
{
    ready_ = false;
    if (!storage_manager.isReady()) return ActivityStorageResult::NOT_READY;

    std::vector<ActivitySession> sessions;
    ActivityStorageResult scan_result = ActivityStorageResult::OK;
    uint64_t maximum_id = 0;
    if (!readLatestSessions(sessions, scan_result, maximum_id)) return scan_result;

    Preferences preferences;
    if (!preferences.begin(ID_NAMESPACE, true)) return ActivityStorageResult::IO_ERROR;
    const uint64_t reserved_next_id = preferences.getULong64(NEXT_ACTIVITY_ID_KEY, 0);
    preferences.end();

    if (maximum_id == std::numeric_limits<uint64_t>::max()) return ActivityStorageResult::ID_ERROR;
    const uint64_t file_next_id = maximum_id + 1;
    next_activity_id_ = std::max<uint64_t>(1, std::max(reserved_next_id, file_next_id));
    ready_ = true;
    // A damaged tail does not hide older valid sessions, but future appends
    // will be rejected until the damaged file is explicitly recovered.
    return scan_result;
}

ActivityStorageResult ActivityStorageManager::createSession(
    const ActivityConfiguration &configuration,
    const std::vector<uint16_t> &registered_student_ids,
    int64_t started_at,
    ActivitySession &created)
{
    if (!ready_ || !storage_manager.isReady()) return ActivityStorageResult::NOT_READY;
    if (started_at < MIN_VALID_ACTIVITY_EPOCH) return ActivityStorageResult::TIME_UNSYNCED;
    if (validateActivityConfiguration(configuration, registered_student_ids) !=
        ActivityValidationResult::VALID) return ActivityStorageResult::INVALID_ARGUMENT;

    std::vector<ActivitySession> sessions;
    ActivityStorageResult scan_result = ActivityStorageResult::OK;
    uint64_t maximum_id = 0;
    if (!readLatestSessions(sessions, scan_result, maximum_id)) return scan_result;
    if (scan_result != ActivityStorageResult::OK) return scan_result;
    if (next_activity_id_ == 0 || next_activity_id_ == std::numeric_limits<uint64_t>::max()) {
        return ActivityStorageResult::ID_ERROR;
    }

    ActivitySession session;
    session.id = next_activity_id_;
    session.schema_version = ACTIVITY_SESSION_SCHEMA_VERSION;
    session.activity_number_enabled = configuration.activity_number_enabled;
    session.activity_number = configuration.activity_number_enabled ? configuration.activity_number : 0;
    session.reward_amount = configuration.reward_amount;
    session.duration_seconds = configuration.duration_seconds;
    session.participant_mode = configuration.participant_mode;
    session.started_at = started_at;
    session.status = ActivitySessionStatus::ACTIVE;
    if (configuration.participant_mode == ActivityParticipantMode::ALL) {
        session.participant_student_ids = registered_student_ids;
    } else if (configuration.participant_mode == ActivityParticipantMode::SELECTED) {
        session.participant_student_ids = configuration.selected_student_ids;
    }

    const uint64_t reserved_next_id = session.id + 1;
    if (!reserveNextActivityId(reserved_next_id)) return ActivityStorageResult::ID_ERROR;
    next_activity_id_ = reserved_next_id;

    const ActivityStorageResult write_result = appendSnapshot(session);
    if (write_result != ActivityStorageResult::OK) return write_result;
    created = session;
    return ActivityStorageResult::OK;
}

ActivityStorageResult ActivityStorageManager::readSessions(
    std::vector<ActivitySession> &sessions) const
{
    if (!ready_ || !storage_manager.isReady()) return ActivityStorageResult::NOT_READY;
    ActivityStorageResult result = ActivityStorageResult::OK;
    uint64_t maximum_id = 0;
    if (!readLatestSessions(sessions, result, maximum_id)) return result;
    return result;
}

ActivityStorageResult ActivityStorageManager::readSession(uint64_t id,
                                                          ActivitySession &session) const
{
    if (id == 0) return ActivityStorageResult::INVALID_ARGUMENT;
    std::vector<ActivitySession> sessions;
    const ActivityStorageResult result = readSessions(sessions);
    for (const ActivitySession &item : sessions) {
        if (item.id == id) {
            session = item;
            return result;
        }
    }
    return result == ActivityStorageResult::OK ? ActivityStorageResult::NOT_FOUND : result;
}

ActivityStorageResult ActivityStorageManager::closeSession(uint64_t id,
                                                            ActivitySessionStatus status,
                                                            int64_t closed_at,
                                                            ActivitySession &updated)
{
    if (!ready_ || !storage_manager.isReady()) return ActivityStorageResult::NOT_READY;
    if (status != ActivitySessionStatus::FINISHED && status != ActivitySessionStatus::CANCELLED) {
        return ActivityStorageResult::INVALID_STATE;
    }
    ActivitySession current;
    const ActivityStorageResult read_result = readSession(id, current);
    if (read_result != ActivityStorageResult::OK) return read_result;
    if (current.status != ActivitySessionStatus::ACTIVE) return ActivityStorageResult::INVALID_STATE;
    if (closed_at < MIN_VALID_ACTIVITY_EPOCH || closed_at < current.started_at) {
        return ActivityStorageResult::TIME_UNSYNCED;
    }

    current.status = status;
    current.closed_at = closed_at;
    const ActivityStorageResult write_result = appendSnapshot(current);
    if (write_result == ActivityStorageResult::OK) updated = current;
    return write_result;
}

ActivityStorageResult ActivityStorageManager::updateActiveSession(
    uint64_t id, const ActivityConfiguration &configuration,
    const std::vector<uint16_t> &registered_student_ids, ActivitySession &updated)
{
    if (!ready_ || !storage_manager.isReady()) return ActivityStorageResult::NOT_READY;
    if (id == 0 || validateActivityConfiguration(configuration, registered_student_ids) !=
                       ActivityValidationResult::VALID) return ActivityStorageResult::INVALID_ARGUMENT;
    bool has_claim = false;
    if (activity_claim_storage_manager.hasClaimForActivity(id, has_claim) != ActivityClaimStorageResult::OK)
        return ActivityStorageResult::CORRUPT_DATA;
    if (has_claim) return ActivityStorageResult::INVALID_STATE;
    ActivitySession current;
    const ActivityStorageResult read_result = readSession(id, current);
    if (read_result != ActivityStorageResult::OK) return read_result;
    if (!canEditActivityBeforeFirstClaim(current, has_claim)) return ActivityStorageResult::INVALID_STATE;
    current.activity_number_enabled = configuration.activity_number_enabled;
    current.activity_number = configuration.activity_number_enabled ? configuration.activity_number : 0;
    current.reward_amount = configuration.reward_amount;
    current.duration_seconds = configuration.timed ? configuration.duration_seconds : 0;
    current.participant_mode = configuration.participant_mode;
    current.participant_student_ids = configuration.participant_mode == ActivityParticipantMode::SELECTED
        ? configuration.selected_student_ids
        : configuration.participant_mode == ActivityParticipantMode::ALL ? registered_student_ids
                                                                           : std::vector<uint16_t>();
    const ActivityStorageResult write_result = appendSnapshot(current);
    if (write_result == ActivityStorageResult::OK) updated = current;
    return write_result;
}

ActivityStorageResult ActivityStorageManager::appendSnapshot(const ActivitySession &session)
{
    std::vector<ActivitySession> sessions;
    ActivityStorageResult scan_result = ActivityStorageResult::OK;
    uint64_t maximum_id = 0;
    if (!readLatestSessions(sessions, scan_result, maximum_id)) return scan_result;
    if (scan_result != ActivityStorageResult::OK) return scan_result;

    if (!LittleFS.exists(ACTIVITIES_DIRECTORY) && !LittleFS.mkdir(ACTIVITIES_DIRECTORY)) {
        return ActivityStorageResult::IO_ERROR;
    }

    StaticJsonDocument<1024> document;
    document["schema_version"] = session.schema_version;
    document["record"] = "activity";
    document["id"] = session.id;
    document["activity_number_enabled"] = session.activity_number_enabled;
    document["activity_number"] = session.activity_number;
    document["reward_amount"] = session.reward_amount;
    document["duration_seconds"] = session.duration_seconds;
    document["participant_mode"] = participantModeName(session.participant_mode);
    JsonArray participant_ids = document.createNestedArray("participant_student_ids");
    for (const uint16_t student_id : session.participant_student_ids) participant_ids.add(student_id);
    document["started_at"] = session.started_at;
    document["status"] = statusName(session.status);
    if (session.closed_at > 0) document["closed_at"] = session.closed_at;
    if (document.overflowed()) return ActivityStorageResult::IO_ERROR;
    const size_t expected_size = measureJson(document);
    if (expected_size + 1U >= MAX_ACTIVITY_LINE_LENGTH) return ActivityStorageResult::IO_ERROR;

    File file = LittleFS.open(ACTIVITIES_FILE, FILE_APPEND);
    if (!file) return ActivityStorageResult::IO_ERROR;
    const size_t written_size = serializeJson(document, file);
    const bool newline_written = file.write(static_cast<uint8_t>('\n')) == 1;
    file.flush();
    file.close();
    if (written_size != expected_size || !newline_written) return ActivityStorageResult::IO_ERROR;
    return ActivityStorageResult::OK;
}
