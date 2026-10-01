#include "storage_manager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_partition.h>
#include <nvs.h>

#include <cstring>
#include <ctime>
#include <limits>

#include "time/time_manager.h"

namespace {
constexpr char STORAGE_MOUNT_PATH[] = "/littlefs";
constexpr char MOVEMENTS_DIRECTORY[] = "/data";
constexpr char MOVEMENTS_FILE[] = "/data/movements.ndjson";
constexpr char ID_NAMESPACE[] = "bank_storage";
constexpr char NEXT_MOVEMENT_ID_KEY[] = "next_mv_id";
constexpr char FS_INITIALIZED_KEY[] = "fs_initialized";
constexpr char FS_FORMAT_ATTEMPTED_KEY[] = "fs_fmt_try";
constexpr char TEST_DONE_KEY[] = "st_test_done";
constexpr char TEST_ID_KEY[] = "storage_test_id";
constexpr uint32_t EXPECTED_FS_OFFSET = 0xC90000;
constexpr size_t EXPECTED_FS_SIZE = 0x360000;
constexpr size_t MAX_MOVEMENT_LINE_LENGTH = 384;
constexpr time_t MIN_VALID_EPOCH = 1735689600; // 2025-01-01 UTC, same gate as TimeManager.

const char *movementTypeName(StoredMovementType type)
{
    return type == StoredMovementType::ENTRY ? "entry" : "exit";
}

const char *originName(StoredRecordOrigin origin)
{
    return origin == StoredRecordOrigin::PANEL ? "panel" : "terminal";
}

bool parseMovementLine(const char *line, MovementRecord &record)
{
    StaticJsonDocument<512> document;
    if (deserializeJson(document, line) != DeserializationError::Ok) return false;
    if ((document["schema_version"] | 0) != STORAGE_SCHEMA_VERSION) return false;
    if (strcmp(document["record"] | "", "movement") != 0) return false;
    if (!document["id"].is<uint64_t>() || !document["student_id"].is<uint16_t>() ||
        !document["amount"].is<int32_t>() || !document["timestamp"].is<int64_t>()) return false;

    const char *type = document["type"] | "";
    const char *origin = document["origin"] | "";
    const char *reason = document["reason"] | "";
    if (strcmp(type, "entry") == 0) record.type = StoredMovementType::ENTRY;
    else if (strcmp(type, "exit") == 0) record.type = StoredMovementType::EXIT;
    else return false;
    if (strcmp(origin, "terminal") == 0) record.origin = StoredRecordOrigin::TERMINAL;
    else if (strcmp(origin, "panel") == 0) record.origin = StoredRecordOrigin::PANEL;
    else return false;
    if (strlen(reason) > MOVEMENT_REASON_MAX_LENGTH) return false;

    record.id = document["id"].as<uint64_t>();
    record.student_id = document["student_id"].as<uint16_t>();
    record.amount = document["amount"].as<int32_t>();
    record.timestamp = document["timestamp"].as<int64_t>();
    record.synced = document["synced"] | false;
    record.schema_version = STORAGE_SCHEMA_VERSION;
    memcpy(record.reason, reason, strlen(reason) + 1);
    return record.id != 0 && record.timestamp >= MIN_VALID_EPOCH;
}

bool readBoundedLine(File &file, char *line, size_t capacity, bool &overflow)
{
    size_t length = 0;
    overflow = false;
    bool read_any = false;
    while (file.available()) {
        const int value = file.read();
        if (value < 0) break;
        read_any = true;
        if (value == '\n') break;
        if (length + 1 < capacity) line[length++] = static_cast<char>(value);
        else overflow = true;
    }
    line[length] = '\0';
    return read_any;
}

bool persistNextMovementId(uint64_t next_id)
{
    Preferences preferences;
    if (!preferences.begin(ID_NAMESPACE, false)) return false;
    const size_t written = preferences.putULong64(NEXT_MOVEMENT_ID_KEY, next_id);
    preferences.end();
    return written == sizeof(uint64_t);
}

uint64_t storedNextMovementId()
{
    Preferences preferences;
    if (!preferences.begin(ID_NAMESPACE, true)) return 0;
    const uint64_t value = preferences.getULong64(NEXT_MOVEMENT_ID_KEY, 0);
    preferences.end();
    return value;
}

uint64_t findMaximumMovementId()
{
    File file = LittleFS.open(MOVEMENTS_FILE, FILE_READ);
    if (!file) return 0;

    uint64_t maximum_id = 0;
    char line[MAX_MOVEMENT_LINE_LENGTH];
    bool overflow = false;
    while (readBoundedLine(file, line, sizeof(line), overflow)) {
        MovementRecord record = {};
        if (!overflow && parseMovementLine(line, record) && record.id > maximum_id) {
            maximum_id = record.id;
        }
    }
    file.close();
    return maximum_id;
}

size_t countMovements(bool pending_only)
{
    File file = LittleFS.open(MOVEMENTS_FILE, FILE_READ);
    if (!file) return 0;

    size_t count = 0;
    char line[MAX_MOVEMENT_LINE_LENGTH];
    bool overflow = false;
    while (readBoundedLine(file, line, sizeof(line), overflow)) {
        MovementRecord record = {};
        if (!overflow && parseMovementLine(line, record) && (!pending_only || !record.synced)) ++count;
    }
    file.close();
    return count;
}

StorageResult readMovementRecords(size_t offset, MovementRecord *records, size_t capacity,
                                  size_t &read_count, bool pending_only)
{
    read_count = 0;
    if (records == nullptr && capacity != 0) return StorageResult::STORAGE_INVALID_ARGUMENT;
    File file = LittleFS.open(MOVEMENTS_FILE, FILE_READ);
    if (!file) return StorageResult::STORAGE_IO_ERROR;

    size_t matching_index = 0;
    char line[MAX_MOVEMENT_LINE_LENGTH];
    bool overflow = false;
    while (readBoundedLine(file, line, sizeof(line), overflow)) {
        MovementRecord record = {};
        if (overflow || !parseMovementLine(line, record) || (pending_only && record.synced)) continue;
        if (matching_index++ < offset) continue;
        if (read_count >= capacity) break;
        records[read_count++] = record;
    }
    file.close();
    return StorageResult::STORAGE_OK;
}

bool readStorageTestMarker(bool &done, uint64_t &test_id)
{
    Preferences preferences;
    if (!preferences.begin(ID_NAMESPACE, true)) return false;
    done = preferences.getBool(TEST_DONE_KEY, false);
    test_id = preferences.getULong64(TEST_ID_KEY, 0);
    preferences.end();
    return true;
}

bool writeStorageTestMarker(uint64_t test_id)
{
    Preferences preferences;
    if (!preferences.begin(ID_NAMESPACE, false)) return false;
    const size_t id_written = preferences.putULong64(TEST_ID_KEY, test_id);
    const size_t done_written = preferences.putBool(TEST_DONE_KEY, true);
    preferences.end();
    return id_written == sizeof(uint64_t) && done_written == sizeof(uint8_t);
}

bool readFilesystemGuard(bool &initialized, bool &format_attempted)
{
    nvs_handle_t handle;
    esp_err_t error = nvs_open(ID_NAMESPACE, NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        // A read-only open cannot create a namespace on its first use.
        error = nvs_open(ID_NAMESPACE, NVS_READWRITE, &handle);
        if (error == ESP_OK) Serial.println("[Storage] NVS bank_storage inicializado");
    }
    if (error != ESP_OK) {
        Serial.printf("[Storage] Error al abrir NVS; formato bloqueado: %s\n",
                      esp_err_to_name(error));
        return false;
    }

    // Preferences::putBool stores an NVS u8. Only a missing key means false;
    // a type mismatch or another read error must not authorize formatting.
    auto read_bool = [handle](const char *key, bool &value) {
        uint8_t raw = 0;
        const esp_err_t read_error = nvs_get_u8(handle, key, &raw);
        if (read_error == ESP_ERR_NVS_NOT_FOUND) {
            value = false;
            return true;
        }
        if (read_error != ESP_OK || raw > 1) {
            Serial.printf("[Storage] Error al leer NVS (%s); formato bloqueado: %s\n",
                          key, read_error == ESP_OK ? "valor invalido" : esp_err_to_name(read_error));
            return false;
        }
        value = raw == 1;
        return true;
    };

    const bool valid = read_bool(FS_INITIALIZED_KEY, initialized) &&
                       read_bool(FS_FORMAT_ATTEMPTED_KEY, format_attempted);
    nvs_close(handle);
    return valid;
}

bool writeFilesystemGuard(const char *key, bool value)
{
    Preferences preferences;
    if (!preferences.begin(ID_NAMESPACE, false)) return false;
    const size_t written = preferences.putBool(key, value);
    preferences.end();
    return written == sizeof(uint8_t);
}

bool isExpectedFilesystemPartition()
{
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
    return partition != nullptr && partition->type == ESP_PARTITION_TYPE_DATA &&
           partition->subtype == ESP_PARTITION_SUBTYPE_DATA_SPIFFS &&
           strcmp(partition->label, "spiffs") == 0 &&
           partition->address == EXPECTED_FS_OFFSET && partition->size == EXPECTED_FS_SIZE;
}

bool mountOrInitializeFilesystem()
{
    if (LittleFS.begin(false, STORAGE_MOUNT_PATH, 10, "spiffs")) return true;

    Serial.println("[Storage] Mount falló");
    if (!STORAGE_ALLOW_ONE_TIME_FORMAT) {
        Serial.println("[Storage] Formateo no autorizado; se continúa sin almacenamiento");
        return false;
    }

    bool initialized = false;
    bool format_attempted = false;
    if (!readFilesystemGuard(initialized, format_attempted)) {
        Serial.println("[Storage] No se pudo leer la protección NVS; formato bloqueado");
        return false;
    }
    if (initialized) {
        Serial.println("[Storage] fs_initialized activo; formato bloqueado");
        return false;
    }
    if (format_attempted) {
        Serial.println("[Storage] Ya existe un intento de formato; formato bloqueado");
        return false;
    }
    if (!isExpectedFilesystemPartition()) {
        Serial.println("[Storage] Partición inesperada; formato bloqueado");
        return false;
    }

    Serial.println("[Storage] Inicialización de filesystem autorizada");
    Serial.printf("[Storage] Partición validada: spiffs, offset 0x%06lX, tamaño 0x%06lX\n",
                  static_cast<unsigned long>(EXPECTED_FS_OFFSET),
                  static_cast<unsigned long>(EXPECTED_FS_SIZE));

    // Persist the attempt before erasing anything. A failed format/remount will
    // not be repeated on a later boot, even if the development flag stays on.
    if (!writeFilesystemGuard(FS_FORMAT_ATTEMPTED_KEY, true)) {
        Serial.println("[Storage] No se pudo guardar protección NVS; formato bloqueado");
        return false;
    }

    Serial.println("[Storage] Formateando únicamente partición LittleFS");
    // LittleFS.format() delegates to esp_littlefs_format(partitionLabel_),
    // where partitionLabel_ was set to "spiffs" by the failed begin() above.
    if (!LittleFS.format()) {
        Serial.println("[Storage] Formato falló");
        return false;
    }

    Serial.println("[Storage] Formato OK");
    Serial.println("[Storage] Remontando LittleFS");
    if (!LittleFS.begin(false, STORAGE_MOUNT_PATH, 10, "spiffs")) {
        Serial.println("[Storage] Remontaje falló; filesystem deshabilitado");
        return false;
    }
    return true;
}

bool findStorageTestRecord(uint64_t requested_id, MovementRecord &found, size_t &matches)
{
    File file = LittleFS.open(MOVEMENTS_FILE, FILE_READ);
    if (!file) return false;
    matches = 0;
    char line[MAX_MOVEMENT_LINE_LENGTH];
    bool overflow = false;
    while (readBoundedLine(file, line, sizeof(line), overflow)) {
        MovementRecord record = {};
        if (overflow || !parseMovementLine(line, record) ||
            strcmp(record.reason, "PRUEBA_STORAGE") != 0) continue;
        ++matches;
        if (requested_id == 0 || record.id == requested_id) found = record;
    }
    file.close();
    return true;
}

bool isExpectedStorageTestRecord(const MovementRecord &record)
{
    return record.id != 0 && record.student_id == 7 && record.amount == 1 &&
           record.type == StoredMovementType::ENTRY &&
           record.origin == StoredRecordOrigin::TERMINAL && !record.synced;
}
}

StorageManager storage_manager;

bool StorageManager::begin()
{
    if (state_ == StorageState::STORAGE_READY) return true;

    Serial.println("[Storage] Montando LittleFS");
    // false explicitly disables auto-formatting; optional format is a separate,
    // development-only path guarded by NVS and the exact partition geometry.
    if (!mountOrInitializeFilesystem()) {
        state_ = StorageState::STORAGE_ERROR;
        Serial.println("[Storage] Error al montar filesystem");
        return false;
    }

    if (!LittleFS.exists(MOVEMENTS_DIRECTORY) && !LittleFS.mkdir(MOVEMENTS_DIRECTORY)) {
        state_ = StorageState::STORAGE_ERROR;
        Serial.println("[Storage] Error al crear directorio de datos");
        return false;
    }
    if (!LittleFS.exists(MOVEMENTS_FILE)) {
        File empty_file = LittleFS.open(MOVEMENTS_FILE, FILE_WRITE);
        if (!empty_file) {
            state_ = StorageState::STORAGE_ERROR;
            Serial.println("[Storage] Error al crear archivo de movimientos");
            return false;
        }
        empty_file.close();
    }

    const uint64_t maximum_id = findMaximumMovementId();
    const uint64_t minimum_next_id = maximum_id == std::numeric_limits<uint64_t>::max()
                                         ? maximum_id
                                         : maximum_id + 1;
    const uint64_t persisted_next_id = storedNextMovementId();
    next_movement_id_ = persisted_next_id > minimum_next_id ? persisted_next_id : minimum_next_id;
    if (next_movement_id_ == 0) next_movement_id_ = 1;

    total_bytes_ = LittleFS.totalBytes();
    used_bytes_ = LittleFS.usedBytes();
    if (!writeFilesystemGuard(FS_INITIALIZED_KEY, true)) {
        state_ = StorageState::STORAGE_ERROR;
        Serial.println("[Storage] No se pudo guardar fs_initialized; almacenamiento deshabilitado");
        return false;
    }
    state_ = StorageState::STORAGE_READY;

    Serial.println("[Storage] LittleFS listo");
    Serial.printf("[Storage] Movimientos: %u\n", static_cast<unsigned>(getMovementCount()));
    Serial.printf("[Storage] Total: %u bytes\n", static_cast<unsigned>(totalBytes()));
    Serial.printf("[Storage] Usado: %u bytes\n", static_cast<unsigned>(usedBytes()));
    Serial.printf("[Storage] Libre: %u bytes\n", static_cast<unsigned>(freeBytes()));
    return true;
}

StorageState StorageManager::state() const
{
    return state_;
}

bool StorageManager::isReady() const
{
    return state_ == StorageState::STORAGE_READY;
}

size_t StorageManager::totalBytes() const
{
    return total_bytes_;
}

size_t StorageManager::usedBytes() const
{
    return used_bytes_;
}

size_t StorageManager::freeBytes() const
{
    return total_bytes_ > used_bytes_ ? total_bytes_ - used_bytes_ : 0;
}

size_t StorageManager::getTotalBytes() const
{
    return totalBytes();
}

size_t StorageManager::getUsedBytes() const
{
    return usedBytes();
}

size_t StorageManager::getFreeBytes() const
{
    return freeBytes();
}

uint64_t StorageManager::nextMovementId() const
{
    return next_movement_id_;
}

void StorageManager::updateSelfTest()
{
    if (!STORAGE_SELF_TEST || self_test_finished_ || state_ != StorageState::STORAGE_READY) return;

    if (!self_test_initialized_) {
        bool marker_done = false;
        uint64_t marker_id = 0;
        if (!readStorageTestMarker(marker_done, marker_id)) {
            Serial.println("[StorageTest] Error al leer marcador NVS; prueba detenida");
            self_test_finished_ = true;
            return;
        }

        if (marker_done) {
            MovementRecord prior = {};
            size_t matches = 0;
            if (!findStorageTestRecord(marker_id, prior, matches) || matches != 1 ||
                !isExpectedStorageTestRecord(prior)) {
                Serial.println("[StorageTest] Error: registro previo ausente o invalido; prueba detenida");
                self_test_finished_ = true;
                return;
            }
            Serial.println("[StorageTest] Registro previo encontrado");
            Serial.printf("[StorageTest] ID: %llu, timestamp: %lld\n",
                          static_cast<unsigned long long>(prior.id),
                          static_cast<long long>(prior.timestamp));
            Serial.println("[StorageTest] Persistencia OK");
            Serial.printf("[StorageTest] next_mv_id: %llu\n",
                          static_cast<unsigned long long>(next_movement_id_));
            self_test_finished_ = true;
            return;
        }

        // Recover safely if power was lost after appending the record but before
        // the NVS completion marker was committed.
        MovementRecord existing = {};
        size_t existing_matches = 0;
        if (!findStorageTestRecord(0, existing, existing_matches)) {
            Serial.println("[StorageTest] Error de lectura; prueba detenida");
            self_test_finished_ = true;
            return;
        }
        if (existing_matches > 1 || (existing_matches == 1 && !isExpectedStorageTestRecord(existing))) {
            Serial.println("[StorageTest] Error: registro de prueba ambiguo; prueba detenida");
            self_test_finished_ = true;
            return;
        }
        if (existing_matches == 1) {
            if (!writeStorageTestMarker(existing.id)) {
                Serial.println("[StorageTest] Error al guardar marcador NVS; prueba detenida");
                self_test_finished_ = true;
                return;
            }
            Serial.println("[StorageTest] Registro previo encontrado");
            Serial.printf("[StorageTest] ID: %llu, timestamp: %lld\n",
                          static_cast<unsigned long long>(existing.id),
                          static_cast<long long>(existing.timestamp));
            Serial.println("[StorageTest] Persistencia OK");
            self_test_finished_ = true;
            return;
        }
        self_test_initialized_ = true;
    }

    struct tm local_time = {};
    if (!time_manager.getLocalTime(local_time)) return; // Wait without blocking for NTP.

    Serial.println("[StorageTest] Escribiendo registro de prueba");
    const size_t used_before = usedBytes();
    uint64_t created_id = 0;
    const StorageResult result = appendMovement(7, 1, StoredMovementType::ENTRY,
                                                "PRUEBA_STORAGE", StoredRecordOrigin::TERMINAL,
                                                &created_id);
    if (result != StorageResult::STORAGE_OK) {
        if (result != StorageResult::STORAGE_TIME_UNSYNCED) {
            Serial.printf("[StorageTest] Error de escritura (%u); prueba detenida\n",
                          static_cast<unsigned>(result));
            self_test_finished_ = true;
        }
        return;
    }
    Serial.println("[StorageTest] Escritura OK");

    MovementRecord written = {};
    size_t matches = 0;
    if (!findStorageTestRecord(created_id, written, matches) || matches != 1 ||
        !isExpectedStorageTestRecord(written)) {
        Serial.println("[StorageTest] Error de lectura/verificacion; prueba detenida");
        self_test_finished_ = true;
        return;
    }
    Serial.println("[StorageTest] Lectura OK");
    Serial.printf("[StorageTest] ID: %llu, timestamp: %lld\n",
                  static_cast<unsigned long long>(written.id),
                  static_cast<long long>(written.timestamp));

    if (!writeStorageTestMarker(created_id)) {
        Serial.println("[StorageTest] Error al guardar marcador NVS; prueba detenida");
        self_test_finished_ = true;
        return;
    }
    used_bytes_ = LittleFS.usedBytes();
    Serial.printf("[StorageTest] usedBytes antes: %u, despues: %u, libre: %u bytes\n",
                  static_cast<unsigned>(used_before), static_cast<unsigned>(used_bytes_),
                  static_cast<unsigned>(freeBytes()));
    Serial.println("[StorageTest] Persistencia pendiente de reinicio");
    Serial.printf("[StorageTest] next_mv_id: %llu\n",
                  static_cast<unsigned long long>(next_movement_id_));
    self_test_finished_ = true;
}

StorageResult StorageManager::appendMovement(uint16_t student_id, int32_t amount,
                                              StoredMovementType type, const char *reason,
                                              StoredRecordOrigin origin, uint64_t *created_id)
{
    if (state_ != StorageState::STORAGE_READY) return StorageResult::STORAGE_NOT_READY;
    if (student_id == 0 || (type != StoredMovementType::ENTRY && type != StoredMovementType::EXIT) ||
        (origin != StoredRecordOrigin::TERMINAL && origin != StoredRecordOrigin::PANEL)) {
        return StorageResult::STORAGE_INVALID_ARGUMENT;
    }

    const char *safe_reason = reason == nullptr ? "" : reason;
    if (strlen(safe_reason) > MOVEMENT_REASON_MAX_LENGTH) return StorageResult::STORAGE_INVALID_ARGUMENT;

    struct tm local_time = {};
    if (!time_manager.getLocalTime(local_time)) return StorageResult::STORAGE_TIME_UNSYNCED;
    time_t epoch = 0;
    time(&epoch);
    if (epoch < MIN_VALID_EPOCH) return StorageResult::STORAGE_TIME_UNSYNCED;
    if (next_movement_id_ == 0 || next_movement_id_ == std::numeric_limits<uint64_t>::max()) {
        return StorageResult::STORAGE_ID_ERROR;
    }

    MovementRecord record = {};
    record.id = next_movement_id_;
    record.student_id = student_id;
    record.amount = amount;
    record.type = type;
    memcpy(record.reason, safe_reason, strlen(safe_reason) + 1);
    record.timestamp = static_cast<int64_t>(epoch);
    record.origin = origin;
    record.synced = false;
    record.schema_version = STORAGE_SCHEMA_VERSION;

    // Persist the next ID before appending. A failed write may leave a gap,
    // but can never cause an ID to be reused after a reboot.
    if (!persistNextMovementId(next_movement_id_ + 1)) return StorageResult::STORAGE_ID_ERROR;
    ++next_movement_id_;

    StaticJsonDocument<512> document;
    document["schema_version"] = STORAGE_SCHEMA_VERSION;
    document["record"] = "movement";
    document["id"] = record.id;
    document["student_id"] = record.student_id;
    document["amount"] = record.amount;
    document["type"] = movementTypeName(record.type);
    document["reason"] = record.reason;
    document["timestamp"] = record.timestamp;
    document["origin"] = originName(record.origin);
    document["synced"] = record.synced;

    // If power failed during a previous append, isolate its partial final line
    // so a subsequent valid JSON record remains recoverable.
    bool needs_separator = false;
    File tail = LittleFS.open(MOVEMENTS_FILE, FILE_READ);
    if (!tail) return StorageResult::STORAGE_IO_ERROR;
    if (tail.size() > 0) {
        const bool positioned = tail.seek(tail.size() - 1, SeekSet);
        const int final_byte = positioned ? tail.read() : -1;
        if (final_byte < 0) {
            tail.close();
            return StorageResult::STORAGE_IO_ERROR;
        }
        needs_separator = final_byte != '\n';
    }
    tail.close();

    File file = LittleFS.open(MOVEMENTS_FILE, FILE_APPEND);
    if (!file) return StorageResult::STORAGE_IO_ERROR;
    if (needs_separator) {
        if (file.write(static_cast<uint8_t>('\n')) != 1) {
            file.close();
            return StorageResult::STORAGE_IO_ERROR;
        }
    }

    const size_t expected_size = measureJson(document);
    const size_t written_size = serializeJson(document, file);
    const bool newline_written = file.write(static_cast<uint8_t>('\n')) == 1;
    file.flush();
    file.close();
    if (written_size != expected_size || !newline_written) return StorageResult::STORAGE_IO_ERROR;

    used_bytes_ = LittleFS.usedBytes();
    if (created_id) *created_id = record.id;
    return StorageResult::STORAGE_OK;
}

size_t StorageManager::getMovementCount() const
{
    return state_ == StorageState::STORAGE_READY ? countMovements(false) : 0;
}

size_t StorageManager::getPendingMovementCount() const
{
    return state_ == StorageState::STORAGE_READY ? countMovements(true) : 0;
}

StorageResult StorageManager::readMovements(size_t offset, MovementRecord *records,
                                             size_t capacity, size_t &read_count) const
{
    if (state_ != StorageState::STORAGE_READY) return StorageResult::STORAGE_NOT_READY;
    return readMovementRecords(offset, records, capacity, read_count, false);
}

StorageResult StorageManager::getPendingMovements(size_t offset, MovementRecord *records,
                                                   size_t capacity, size_t &read_count) const
{
    if (state_ != StorageState::STORAGE_READY) return StorageResult::STORAGE_NOT_READY;
    return readMovementRecords(offset, records, capacity, read_count, true);
}

StorageResult StorageManager::markMovementSynced(uint64_t id)
{
    (void)id;
    return state_ == StorageState::STORAGE_READY ? StorageResult::STORAGE_UNSUPPORTED
                                                 : StorageResult::STORAGE_NOT_READY;
}

StorageResult StorageManager::clearLocalData()
{
    if (state_ != StorageState::STORAGE_READY) {
        Serial.println("[Storage] Borrado cancelado: almacenamiento no disponible");
        return StorageResult::STORAGE_NOT_READY;
    }

    // This list is intentionally explicit. Never format or recursively delete.
    if (LittleFS.exists(MOVEMENTS_FILE) && !LittleFS.remove(MOVEMENTS_FILE)) {
        Serial.println("[Storage] Error al eliminar /data/movements.ndjson");
        return StorageResult::STORAGE_IO_ERROR;
    }

    File empty_file = LittleFS.open(MOVEMENTS_FILE, FILE_WRITE);
    if (!empty_file) {
        Serial.println("[Storage] Error al recrear /data/movements.ndjson");
        return StorageResult::STORAGE_IO_ERROR;
    }
    empty_file.close();

    used_bytes_ = LittleFS.usedBytes();
    Serial.println("[Storage] Datos locales eliminados: /data/movements.ndjson");
    Serial.printf("[Storage] Total: %u, usado: %u, libre: %u bytes\n",
                  static_cast<unsigned>(totalBytes()), static_cast<unsigned>(usedBytes()),
                  static_cast<unsigned>(freeBytes()));
    Serial.printf("[Storage] next_mv_id conservado: %llu\n",
                  static_cast<unsigned long long>(next_movement_id_));
    return StorageResult::STORAGE_OK;
}
