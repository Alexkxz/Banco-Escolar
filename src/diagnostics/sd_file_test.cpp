#include "sd_file_test.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <ff.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "../storage/sd_manager.h"
#include "diag_capture_protocol.h"

namespace {
constexpr char TEST_DIRECTORY[] = "sd_diag_manual";
constexpr char TEST_FILE[] = "sd_diag_manual/payload.txt";
constexpr char INITIAL_CONTENT[] = "Banco Escolar SD diagnostic v1\n";
constexpr char REPLACEMENT_CONTENT[] = "Banco Escolar SD diagnostic replaced v2\n";
constexpr char WRITE_TEST_DIRECTORY[] = "sd_diag_write";
constexpr char WRITE_TEST_CONTENT[] = "Banco Escolar exclusive SD write test v1\n";
constexpr char ALM5A_DIRECTORY[] = "sd_diag_alm5a";
constexpr size_t ALM5A_BLOCK_BYTES = 4096;
constexpr uint32_t ALM5A_LARGE_BYTES = 1024U * 1024U;
constexpr size_t ALM5A_MAX_RECORDS = 16;

struct DiagnosticLease {
    bool acquired = false;
    DiagnosticLease(): acquired(sd_manager.beginDiagnosticExclusive(10000)) {}
    ~DiagnosticLease() { if (acquired) sd_manager.endDiagnosticExclusive(); }
};

uint32_t crc_update(uint32_t crc, const uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return crc;
}

void fill_pattern(uint8_t *buffer, size_t length, uint32_t &state)
{
    for (size_t i = 0; i < length; ++i) {
        state = state * 1664525U + 1013904223U;
        buffer[i] = static_cast<uint8_t>(state >> 24U);
    }
}

uint32_t memory_available()
{
    return static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_8BIT));
}

struct StreamCheck {
    uint64_t expected_size = 0;
    uint64_t bytes = 0;
    uint32_t crc = 0xFFFFFFFFU;
    uint32_t expected_crc = 0;
    bool has_expected_crc = false;
};

bool stream_start_check(void *context, uint64_t file_size)
{
    auto *check = static_cast<StreamCheck *>(context);
    check->expected_size = file_size;
    return true;
}

bool stream_crc_chunk(void *context, const uint8_t *data, size_t length)
{
    auto *check = static_cast<StreamCheck *>(context);
    check->crc = crc_update(check->crc, data, length);
    check->bytes += length;
    return true;
}

bool ensure_diagnostic_directory()
{
    bool exists = false;
    SdFileResult result = sd_manager.fileExists(ALM5A_DIRECTORY, exists);
    if (!result.ok()) {
        Serial.printf("[SD-ALM5A] FALLO consulta de carpeta: %s (errno=%d)\n",
                      sd_file_error_name(result.error), result.system_error);
        return false;
    }
    if (exists) {
        std::vector<SdFileInfo> entries;
        result = sd_manager.listDirectory(ALM5A_DIRECTORY, entries);
        if (!result.ok()) {
            Serial.printf("[SD-ALM5A] FALLO lectura de carpeta: %s (errno=%d)\n",
                          sd_file_error_name(result.error), result.system_error);
            return false;
        }
        return true;
    }
    result = sd_manager.createDirectory(ALM5A_DIRECTORY);
    if (!result.ok()) {
        Serial.printf("[SD-ALM5A] FALLO creación de carpeta: %s (errno=%d)\n",
                      sd_file_error_name(result.error), result.system_error);
        return false;
    }
    return true;
}

bool stream_verify_file(const char *path, uint64_t expected_size, uint32_t expected_crc,
                        uint32_t &actual_size, uint32_t &actual_crc, uint32_t &elapsed_ms)
{
    StreamCheck check;
    const uint32_t started = millis();
    uint64_t file_size = 0;
    uint64_t streamed = 0;
    const SdFileResult result = sd_manager.streamFile(path, stream_start_check, stream_crc_chunk,
                                                       &check, false, file_size, streamed);
    elapsed_ms = millis() - started;
    actual_size = check.bytes > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(check.bytes);
    actual_crc = check.crc ^ 0xFFFFFFFFU;
    const bool exact = result.ok() && file_size == expected_size && streamed == expected_size &&
                       check.bytes == expected_size && actual_crc == expected_crc;
    if (!result.ok())
        Serial.printf("[SD-ALM5A] FALLO lectura %s: %s (errno=%d)\n",
                      path, sd_file_error_name(result.error), result.system_error);
    return exact;
}

bool parse_manifest(const uint8_t *data, size_t length, char path[80], uint32_t &size,
                    uint32_t &crc)
{
    if (data == nullptr || length == 0 || length >= 160) return false;
    char line[160];
    memcpy(line, data, length);
    line[length] = '\0';
    char *newline = strpbrk(line, "\r\n");
    if (newline != nullptr) *newline = '\0';
    char *last_separator = strrchr(line, '|');
    if (last_separator == nullptr) return false;
    char *record_crc_text = last_separator + 1;
    *last_separator = '\0';
    char *end = nullptr;
    const unsigned long stored_record_crc = strtoul(record_crc_text, &end, 16);
    if (end == record_crc_text || *end != '\0' || stored_record_crc > UINT32_MAX ||
        (crc_update(0xFFFFFFFFU, reinterpret_cast<const uint8_t *>(line), strlen(line)) ^ 0xFFFFFFFFU) != stored_record_crc)
        return false;
    unsigned long long parsed_size = 0;
    unsigned parsed_crc = 0;
    char parsed_path[80] = {};
    if (sscanf(line, "ALM5A1|%79[^|]|%llu|%8x", parsed_path, &parsed_size, &parsed_crc) != 3 ||
        parsed_size != ALM5A_LARGE_BYTES ||
        strncmp(parsed_path, "sd_diag_alm5a/data_", 19) != 0 ||
        strstr(parsed_path, ".bin") == nullptr) return false;
    snprintf(path, 80, "%s", parsed_path);
    size = static_cast<uint32_t>(parsed_size);
    crc = parsed_crc;
    return true;
}

uint32_t bytes_per_second(uint32_t bytes, uint32_t elapsed_ms)
{
    if (elapsed_ms == 0) return 0;
    const uint64_t rate = static_cast<uint64_t>(bytes) * 1000ULL / elapsed_ms;
    return rate > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(rate);
}

const char *fat_type_name(uint8_t type)
{
    switch (type) {
        case FS_FAT12: return "FAT12";
        case FS_FAT16: return "FAT16";
        case FS_FAT32: return "FAT32";
        case FS_EXFAT: return "exFAT";
        default: return "NO DISPONIBLE";
    }
}

const char *number_or_unavailable(bool available, uint64_t value)
{
    static char buffer[24];
    if (!available) return "NO DISPONIBLE";
    snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
    return buffer;
}

bool check_content(const char *expected, const char *stage)
{
    std::vector<uint8_t> contents;
    const SdFileResult result = sd_manager.readFile(TEST_FILE, contents);
    if (!result.ok()) {
        Serial.printf("[SD-DIAG] FALLO %s: lectura: %s (errno=%d)\n",
                      stage, sd_file_error_name(result.error), result.system_error);
        return false;
    }

    const size_t expected_length = strlen(expected);
    if (contents.size() != expected_length ||
        memcmp(contents.data(), expected, expected_length) != 0) {
        Serial.printf("[SD-DIAG] FALLO %s: contenido distinto (bytes=%u, esperado=%u)\n",
                      stage, static_cast<unsigned>(contents.size()),
                      static_cast<unsigned>(expected_length));
        return false;
    }
    Serial.printf("[SD-DIAG] PASS %s: contenido verificado (%u bytes)\n",
                  stage, static_cast<unsigned>(expected_length));
    return true;
}

bool write_content(const char *contents, bool replace, const char *stage)
{
    const SdFileResult result = sd_manager.writeFile(TEST_FILE, contents, replace);
    if (!result.ok()) {
        Serial.printf("[SD-DIAG] FALLO %s: escritura: %s (errno=%d)\n",
                      stage, sd_file_error_name(result.error), result.system_error);
        return false;
    }
    Serial.printf("[SD-DIAG] PASS %s\n", stage);
    return true;
}

bool remove_test_file()
{
    const SdFileResult result = sd_manager.removeFile(TEST_FILE);
    if (!result.ok()) {
        Serial.printf("[SD-DIAG] FALLO limpieza: no se pudo eliminar el archivo de prueba: %s (errno=%d)\n",
                      sd_file_error_name(result.error), result.system_error);
        return false;
    }
    bool exists = true;
    const SdFileResult exists_result = sd_manager.fileExists(TEST_FILE, exists);
    if (!exists_result.ok() || exists) {
        Serial.printf("[SD-DIAG] FALLO limpieza: no se pudo confirmar la eliminación (error=%s, existe=%s)\n",
                      sd_file_error_name(exists_result.error), exists ? "sí" : "no");
        return false;
    }
    Serial.println("[SD-DIAG] PASS archivo de prueba eliminado y ausencia confirmada");
    return true;
}

bool verify_test_directory_empty(bool payload_absence_confirmed)
{
    std::vector<SdFileInfo> entries;
    const SdFileResult result = sd_manager.listDirectory(TEST_DIRECTORY, entries);
    if (!result.ok()) {
        Serial.printf("[SD-DIAG] FALLO limpieza: no se pudo confirmar el contenido final de %s: %s (errno=%d)\n",
                      TEST_DIRECTORY, sd_file_error_name(result.error), result.system_error);
        return false;
    }
    if (!entries.empty()) {
        Serial.printf("[SD-DIAG] FALLO limpieza: %s conserva %u entrada(s); no se eliminaron elementos adicionales.\n",
                      TEST_DIRECTORY, static_cast<unsigned>(entries.size()));
        return false;
    }
    if (!payload_absence_confirmed) {
        Serial.printf("[SD-DIAG] FALLO limpieza: el listado visible de %s esta vacio, pero no se pudo confirmar el estado de payload.txt y sus temporales.\n",
                      TEST_DIRECTORY);
        return false;
    }
    Serial.printf("[SD-DIAG] PASS carpeta vacia confirmada y conservada: %s\n", TEST_DIRECTORY);
    return true;
}
} // namespace

void sd_file_test_run()
{
    Serial.println("[SD-DIAG] Inicio manual; no se formatea ni se accede a LittleFS.");
    if (!sd_manager.isReady()) {
        Serial.printf("[SD-DIAG] ABORTADO: microSD no montada (%s).\n", sd_manager.errorName());
        return;
    }

    std::vector<SdFileInfo> root_entries;
    SdFileResult result = sd_manager.listDirectory("", root_entries);
    if (!result.ok()) {
        Serial.printf("[SD-DIAG] ABORTADO sin cambios: no se pudo comprobar la raíz (error=%s, errno=%d).\n",
                      sd_file_error_name(result.error), result.system_error);
        return;
    }
    for (const SdFileInfo &entry : root_entries) {
        if (entry.name == TEST_DIRECTORY) {
            Serial.printf("[SD-DIAG] ABORTADO sin cambios: la carpeta %s ya existe.\n", TEST_DIRECTORY);
            return;
        }
    }

    result = sd_manager.createDirectory(TEST_DIRECTORY);
    if (!result.ok()) {
        Serial.printf("[SD-DIAG] ABORTADO: no se pudo crear la carpeta: %s (errno=%d).\n",
                      sd_file_error_name(result.error), result.system_error);
        return;
    }
    Serial.printf("[SD-DIAG] PASS carpeta creada: %s\n", TEST_DIRECTORY);

    std::vector<SdFileInfo> test_entries;
    result = sd_manager.listDirectory(TEST_DIRECTORY, test_entries);
    if (!result.ok() || !test_entries.empty()) {
        Serial.printf("[SD-DIAG] ABORTADO: la carpeta no está vacía o no se pudo comprobar (error=%s, entradas=%u).\n",
                      sd_file_error_name(result.error), static_cast<unsigned>(test_entries.size()));
        Serial.println("[SD-DIAG] No se intento borrar nada; carpeta conservada para inspeccion. No se escribio payload.txt.");
        Serial.println("[SD-DIAG] RESULTADO: FALLO; verificacion inicial de carpeta incompleta.");
        return;
    }

    bool file_created = false;
    bool payload_absence_confirmed = false;
    bool passed = write_content(INITIAL_CONTENT, false, "archivo inicial escrito");
    if (passed) {
        file_created = true;
        passed = check_content(INITIAL_CONTENT, "lectura inicial");
    } else {
        // The directory was created by this run and was empty beforehand, so
        // only this exact path can be considered an artifact of this test.
        bool exists_after_failed_write = false;
        const SdFileResult exists_result = sd_manager.fileExists(TEST_FILE, exists_after_failed_write);
        file_created = exists_result.ok() && exists_after_failed_write;
        payload_absence_confirmed = exists_result.ok() && !exists_after_failed_write;
        if (!exists_result.ok()) {
            Serial.printf("[SD-DIAG] FALLO limpieza: estado de payload.txt desconocido (%s, errno=%d); no se borrara a ciegas.\n",
                          sd_file_error_name(exists_result.error), exists_result.system_error);
        }
    }
    if (passed) passed = write_content(REPLACEMENT_CONTENT, true, "archivo reemplazado");
    if (passed) passed = check_content(REPLACEMENT_CONTENT, "lectura tras reemplazo");

    bool cleanup_ok = true;
    if (file_created) {
        cleanup_ok = remove_test_file();
        payload_absence_confirmed = cleanup_ok;
    } else {
        cleanup_ok = payload_absence_confirmed;
    }
    if (!cleanup_ok) passed = false;

    if (!verify_test_directory_empty(payload_absence_confirmed)) {
        cleanup_ok = false;
        passed = false;
    }

    if (passed) Serial.println("[SD-DIAG] RESULTADO: PASS.");
    else Serial.println("[SD-DIAG] RESULTADO: FALLO; revisa el archivo y la carpeta de diagnóstico antes de repetir.");
    if (cleanup_ok) {
        Serial.printf("[SD-DIAG] La carpeta vacía %s se conserva; SDManager no ofrece eliminación segura de directorios.\n",
                      TEST_DIRECTORY);
    } else {
        Serial.printf("[SD-DIAG] La carpeta %s se conserva tras un fallo de limpieza; inspección manual requerida.\n",
                      TEST_DIRECTORY);
    }
}

void sd_file_read_test_run()
{
    Serial.println("[SD-READ] Inicio manual de solo lectura; no se modificaran archivos.");
    if (!sd_manager.isReady()) {
        Serial.printf("[SD-READ] ABORTADO: microSD no montada (%s).\n", sd_manager.errorName());
        return;
    }

    for (unsigned attempt = 1; attempt <= 2; ++attempt) {
        std::vector<SdFileInfo> entries;
        const SdFileResult result = sd_manager.listDirectory("", entries);
        if (!result.ok()) {
            Serial.printf("[SD-READ] FALLO lectura de raíz %u/2: %s (errno=%d).\n",
                          attempt, sd_file_error_name(result.error), result.system_error);
            if (attempt == 1) {
                Serial.println("[SD-READ] Detenido: reinicializa el controlador antes de volver a probar.");
            } else {
                Serial.println("[SD-READ] Controlador/tarjeta requiere reinicialización antes de otra prueba.");
            }
            Serial.println("[SD-READ] RESULTADO: FALLO; no se intentó recuperación mediante escritura.");
            return;
        }

        Serial.printf("[SD-READ] PASS lectura de raíz %u/2: %u entrada(s).\n",
                      attempt, static_cast<unsigned>(entries.size()));
        for (const SdFileInfo &entry : entries) {
            Serial.printf("[SD-READ]   %s%s\n", entry.is_directory ? "[DIR] " : "[FILE] ",
                          entry.name.c_str());
        }
    }

    Serial.println("[SD-READ] RESULTADO: PASS; dos lecturas consecutivas de la raíz completadas.");
}

void sd_file_info_test_run()
{
    Serial.println("[SD-INFO] Consulta de solo lectura; no modifica archivos ni formato.");
    if (!sd_manager.isReady()) {
        Serial.printf("[SD-INFO] ABORTADO: microSD no montada (%s).\n", sd_manager.errorName());
        return;
    }
    const SdCardType type = sd_manager.cardType();
    const char *card_name = type == SdCardType::NONE || type == SdCardType::UNKNOWN
        ? "NO DISPONIBLE" : sd_manager.cardTypeName();
    Serial.printf("[SD-INFO] Tarjeta: %s\n", card_name);

    const uint32_t sectors = sd_manager.cardSectorCount();
    const uint32_t sector_size = sd_manager.cardSectorSizeBytes();
    const uint64_t capacity = sd_manager.cardCapacityBytes();
    const bool card_available = sectors != 0 && sector_size != 0 && capacity != 0;
    Serial.printf("[SD-INFO] Sectores (controlador): %s\n",
                  number_or_unavailable(card_available, sectors));
    Serial.printf("[SD-INFO] Bytes/sector: %s (constante del driver: %s)\n",
                  number_or_unavailable(sector_size != 0, sector_size),
                  sector_size == 512 ? "512" : "NO DISPONIBLE");
    Serial.printf("[SD-INFO] Capacidad (sectores x bytes/sector): %s bytes\n",
                  number_or_unavailable(card_available, capacity));

    const uint8_t fat_type = sd_manager.filesystemTypeCode();
    Serial.printf("[SD-INFO] Sistema de archivos: %s", fat_type_name(fat_type));
    if (fat_type != 0) Serial.printf(" (FatFs fs_type=%u)", static_cast<unsigned>(fat_type));
    Serial.println();
    const bool stats = sd_manager.filesystemStatsAvailable();
    Serial.printf("[SD-INFO] Volumen total: %s bytes\n",
                  number_or_unavailable(stats, sd_manager.totalBytes()));
    Serial.printf("[SD-INFO] Volumen disponible: %s bytes\n",
                  number_or_unavailable(stats, sd_manager.freeBytes()));
    Serial.println("[SD-INFO] RESULTADO: consulta completada.");
}

void sd_file_write_test_run()
{
    Serial.println("[SD-WRITE] Inicio manual; conserva todo archivo que alcance a crear.");
    if (!sd_manager.isReady()) {
        Serial.printf("[SD-WRITE] ABORTADO: microSD no montada (%s).\n", sd_manager.errorName());
        return;
    }

    const SdFileResult directory_result = sd_manager.createDirectory(WRITE_TEST_DIRECTORY);
    if (!directory_result.ok()) {
        Serial.printf("[SD-WRITE] ABORTADO: no se pudo reservar la ruta exclusiva %s (error=%s, errno=%d). No se modificó una ruta existente.\n",
                      WRITE_TEST_DIRECTORY, sd_file_error_name(directory_result.error),
                      directory_result.system_error);
        return;
    }
    Serial.printf("[SD-WRITE] PASS ruta exclusiva creada: %s\n", WRITE_TEST_DIRECTORY);

    char relative_path[64];
    bool created = false;
    SdFileResult result;
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        const uint32_t nonce = esp_random();
        snprintf(relative_path, sizeof(relative_path), "%s/test_%08lx.bin",
                 WRITE_TEST_DIRECTORY, static_cast<unsigned long>(nonce));
        result = sd_manager.createFileExclusive(relative_path,
            reinterpret_cast<const uint8_t *>(WRITE_TEST_CONTENT), sizeof(WRITE_TEST_CONTENT) - 1U);
        if (result.ok()) {
            created = true;
            break;
        }
        if (result.error != SdFileError::ALREADY_EXISTS) break;
    }
    if (!created) {
        Serial.printf("[SD-WRITE] FALLO creación exclusiva: %s (errno=%d); carpeta conservada.\n",
                      sd_file_error_name(result.error), result.system_error);
        Serial.println("[SD-WRITE] RESULTADO: FALLO; no se modificó ningún archivo preexistente.");
        return;
    }

    const size_t expected_length = sizeof(WRITE_TEST_CONTENT) - 1U;
    const uint32_t expected_crc = diag_capture::crc32(
        reinterpret_cast<const uint8_t *>(WRITE_TEST_CONTENT), expected_length);
    Serial.printf("[SD-WRITE] PASS escritura sincronizada y cerrada: %s (%u bytes, CRC32=%08lX)\n",
                  relative_path, static_cast<unsigned>(expected_length),
                  static_cast<unsigned long>(expected_crc));

    std::vector<uint8_t> contents;
    result = sd_manager.readFile(relative_path, contents);
    if (!result.ok()) {
        Serial.printf("[SD-WRITE] FALLO lectura: %s (errno=%d). Archivo conservado: %s\n",
                      sd_file_error_name(result.error), result.system_error, relative_path);
        Serial.println("[SD-WRITE] RESULTADO: FALLO; archivo conservado para inspección.");
        return;
    }
    const uint32_t actual_crc = diag_capture::crc32(contents.data(), contents.size());
    const bool exact_match = contents.size() == expected_length &&
        memcmp(contents.data(), WRITE_TEST_CONTENT, expected_length) == 0;
    Serial.printf("[SD-WRITE] Lectura: %u bytes, CRC32=%08lX\n",
                  static_cast<unsigned>(contents.size()), static_cast<unsigned long>(actual_crc));
    if (!exact_match || actual_crc != expected_crc) {
        Serial.printf("[SD-WRITE] FALLO verificación (contenido/longitud/CRC32); archivo conservado: %s\n",
                      relative_path);
        Serial.println("[SD-WRITE] RESULTADO: FALLO; archivo conservado para inspección.");
        return;
    }
    Serial.printf("[SD-WRITE] PASS contenido, longitud y CRC32 verificados; archivo conservado: %s\n",
                  relative_path);
    Serial.println("[SD-WRITE] RESULTADO: PASS.");
}

void sd_file_large_test_run()
{
    Serial.println("[SD-LARGE] Prueba manual exclusiva; tamaño físico máximo habilitado: 1 MiB.");
    if (!sd_manager.isReady()) {
        Serial.printf("[SD-LARGE] ABORTADO: microSD no montada (%s).\n", sd_manager.errorName());
        return;
    }
    DiagnosticLease lease;
    if (!lease.acquired) {
        Serial.println("[SD-LARGE] ABORTADO: SD ocupada o no se pudo adquirir exclusividad en 10 s.");
        return;
    }
    if (!ensure_diagnostic_directory()) return;

    std::vector<SdFileInfo> existing;
    SdFileResult result = sd_manager.listDirectory(ALM5A_DIRECTORY, existing);
    if (!result.ok() || existing.size() >= ALM5A_MAX_RECORDS) {
        Serial.printf("[SD-LARGE] ABORTADO: no se pudo verificar el límite de registros (error=%s, registros=%u, máximo=%u).\n",
                      sd_file_error_name(result.error), static_cast<unsigned>(existing.size()),
                      static_cast<unsigned>(ALM5A_MAX_RECORDS));
        return;
    }

    char data_path[80] = {};
    char manifest_path[80] = {};
    uint32_t run_id = 0;
    bool reserved = false;
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        run_id = esp_random();
        snprintf(data_path, sizeof(data_path), "%s/data_%08lX.bin", ALM5A_DIRECTORY,
                 static_cast<unsigned long>(run_id));
        snprintf(manifest_path, sizeof(manifest_path), "%s/run_%08lX.meta", ALM5A_DIRECTORY,
                 static_cast<unsigned long>(run_id));
        bool data_exists = false;
        bool meta_exists = false;
        const SdFileResult data_check = sd_manager.fileExists(data_path, data_exists);
        const SdFileResult meta_check = sd_manager.fileExists(manifest_path, meta_exists);
        if (!data_check.ok() || !meta_check.ok()) {
            Serial.printf("[SD-LARGE] ABORTADO: no se pudo comprobar nombre nuevo (data=%s, meta=%s).\n",
                          sd_file_error_name(data_check.error), sd_file_error_name(meta_check.error));
            return;
        }
        if (!data_exists && !meta_exists) { reserved = true; break; }
    }
    if (!reserved) {
        Serial.println("[SD-LARGE] ABORTADO: no se encontró un nombre de ejecución libre tras 8 intentos.");
        return;
    }

    static uint8_t buffer[ALM5A_BLOCK_BYTES];
    const uint32_t memory_before = memory_available();
    uint32_t pattern_state = run_id ^ 0xA15A5A1AU;
    uint32_t expected_crc_state = 0xFFFFFFFFU;
    uint64_t generated = 0;
    const uint32_t write_started = millis();
    result = sd_manager.beginExclusiveStreamWrite(data_path);
    while (result.ok() && generated < ALM5A_LARGE_BYTES) {
        const size_t count = static_cast<size_t>(ALM5A_LARGE_BYTES - generated < ALM5A_BLOCK_BYTES
            ? ALM5A_LARGE_BYTES - generated : ALM5A_BLOCK_BYTES);
        fill_pattern(buffer, count, pattern_state);
        expected_crc_state = crc_update(expected_crc_state, buffer, count);
        size_t bytes_written = 0;
        result = sd_manager.writeExclusiveStreamChunk(buffer, count, bytes_written);
        if (result.ok() && bytes_written != count) result = {SdFileError::WRITE_FAILED, 0};
        generated += bytes_written;
        if (!result.ok()) break;
    }
    const uint32_t write_elapsed = millis() - write_started;
    const uint32_t expected_crc = expected_crc_state ^ 0xFFFFFFFFU;
    if (result.ok()) result = sd_manager.finishExclusiveStreamWrite();
    else sd_manager.finishExclusiveStreamWrite(); // Close and retain any partial file.
    if (!result.ok()) {
        Serial.printf("[SD-LARGE] FALLO escritura/sync/cierre: %s (errno=%d); archivo parcial conservado: %s\n",
                      sd_file_error_name(result.error), result.system_error, data_path);
        Serial.printf("[SD-LARGE] Esperado=%u real=%llu CRC esperado=%08lX tiempo=%lu ms memoria antes=%lu después=%lu\n",
                      static_cast<unsigned>(ALM5A_LARGE_BYTES), static_cast<unsigned long long>(generated),
                      static_cast<unsigned long>(expected_crc), static_cast<unsigned long>(write_elapsed),
                      static_cast<unsigned long>(memory_before), static_cast<unsigned long>(memory_available()));
        return;
    }

    uint32_t read_size = 0, actual_crc = 0, read_elapsed = 0;
    const bool verified = stream_verify_file(data_path, ALM5A_LARGE_BYTES, expected_crc,
                                             read_size, actual_crc, read_elapsed);
    char record_prefix[160];
    snprintf(record_prefix, sizeof(record_prefix), "ALM5A1|%s|%u|%08lX", data_path,
             static_cast<unsigned>(ALM5A_LARGE_BYTES), static_cast<unsigned long>(expected_crc));
    const uint32_t record_crc = crc_update(0xFFFFFFFFU,
        reinterpret_cast<const uint8_t *>(record_prefix), strlen(record_prefix)) ^ 0xFFFFFFFFU;
    char manifest[192];
    const int manifest_length = snprintf(manifest, sizeof(manifest), "%s|%08lX\n", record_prefix,
                                         static_cast<unsigned long>(record_crc));
    bool manifest_persisted = false;
    if (verified && manifest_length > 0 && static_cast<size_t>(manifest_length) < sizeof(manifest)) {
        const SdFileResult meta_result = sd_manager.createFileExclusive(manifest_path,
            reinterpret_cast<const uint8_t *>(manifest), static_cast<size_t>(manifest_length));
        if (!meta_result.ok()) {
            Serial.printf("[SD-LARGE] FALLO registro persistente: %s (errno=%d); datos conservados: %s\n",
                          sd_file_error_name(meta_result.error), meta_result.system_error, data_path);
        } else {
            manifest_persisted = true;
            Serial.printf("[SD-LARGE] Registro persistente: %s\n", manifest_path);
        }
    }
    const bool passed = verified && manifest_persisted;
    Serial.printf("[SD-LARGE] Ruta=%s esperado=%u real=%u CRC esperado=%08lX leído=%08lX\n",
                  data_path, static_cast<unsigned>(ALM5A_LARGE_BYTES), read_size,
                  static_cast<unsigned long>(expected_crc), static_cast<unsigned long>(actual_crc));
    Serial.printf("[SD-LARGE] Escritura=%lu ms (%lu B/s), lectura=%lu ms (%lu B/s), memoria antes=%lu después=%lu\n",
                  static_cast<unsigned long>(write_elapsed),
                  static_cast<unsigned long>(bytes_per_second(ALM5A_LARGE_BYTES, write_elapsed)),
                  static_cast<unsigned long>(read_elapsed),
                  static_cast<unsigned long>(bytes_per_second(read_size, read_elapsed)),
                  static_cast<unsigned long>(memory_before), static_cast<unsigned long>(memory_available()));
    Serial.printf("[SD-LARGE] RESULTADO: %s%s\n", passed ? "PASS" : "FAIL",
                  verified ? "" : "; tamaño o CRC de lectura no coincide");
}

void sd_file_verify_test_run()
{
    Serial.println("[SD-VERIFY] Verificación manual de registros persistentes; solo lectura.");
    if (!sd_manager.isReady()) {
        Serial.printf("[SD-VERIFY] ABORTADO: microSD no montada (%s).\n", sd_manager.errorName());
        return;
    }
    DiagnosticLease lease;
    if (!lease.acquired) {
        Serial.println("[SD-VERIFY] ABORTADO: SD ocupada o no se pudo adquirir exclusividad en 10 s.");
        return;
    }
    std::vector<SdFileInfo> entries;
    SdFileResult result = sd_manager.listDirectory(ALM5A_DIRECTORY, entries);
    if (!result.ok()) {
        Serial.printf("[SD-VERIFY] ABORTADO: no se pudo listar %s (%s, errno=%d).\n",
                      ALM5A_DIRECTORY, sd_file_error_name(result.error), result.system_error);
        return;
    }
    size_t checked = 0;
    size_t data_files = 0;
    char registered_paths[ALM5A_MAX_RECORDS][80] = {};
    size_t registered_count = 0;
    bool all_passed = true;
    for (const SdFileInfo &entry : entries) {
        if (!entry.is_directory && entry.name.startsWith("data_") && entry.name.endsWith(".bin"))
            ++data_files;
        if (entry.is_directory || !entry.name.endsWith(".meta")) continue;
        if (checked >= ALM5A_MAX_RECORDS) {
            Serial.printf("[SD-VERIFY] FALLO: hay más de %u registros; revisión acotada.\n",
                          static_cast<unsigned>(ALM5A_MAX_RECORDS));
            all_passed = false;
            break;
        }
        ++checked;
        const String manifest_path = String(ALM5A_DIRECTORY) + "/" + entry.name;
        std::vector<uint8_t> record;
        result = sd_manager.readFile(manifest_path.c_str(), record);
        char data_path[80] = {};
        uint32_t expected_size = 0, expected_crc = 0;
        if (!result.ok() || !parse_manifest(record.data(), record.size(), data_path,
                                             expected_size, expected_crc)) {
            Serial.printf("[SD-VERIFY] FALLO registro incompleto/corrupto: %s (error=%s)\n",
                          manifest_path.c_str(), sd_file_error_name(result.error));
            all_passed = false;
            continue;
        }
        if (registered_count < ALM5A_MAX_RECORDS) {
            snprintf(registered_paths[registered_count], sizeof(registered_paths[registered_count]),
                     "%s", data_path);
            ++registered_count;
        } else {
            Serial.println("[SD-VERIFY] FALLO: límite de rutas registradas excedido.");
            all_passed = false;
        }
        const uint32_t memory_before = memory_available();
        uint32_t actual_size = 0, actual_crc = 0, read_elapsed = 0;
        const bool passed = stream_verify_file(data_path, expected_size, expected_crc,
                                               actual_size, actual_crc, read_elapsed);
        Serial.printf("[SD-VERIFY] Ruta=%s esperado=%u real=%u CRC esperado=%08lX leído=%08lX lectura=%lu ms (%lu B/s) memoria antes=%lu después=%lu RESULTADO=%s\n",
                      data_path, expected_size, actual_size, static_cast<unsigned long>(expected_crc),
                      static_cast<unsigned long>(actual_crc), static_cast<unsigned long>(read_elapsed),
                      static_cast<unsigned long>(bytes_per_second(actual_size, read_elapsed)),
                      static_cast<unsigned long>(memory_before), static_cast<unsigned long>(memory_available()),
                      passed ? "PASS" : "FAIL");
        if (!passed) all_passed = false;
    }
    for (const SdFileInfo &entry : entries) {
        if (entry.is_directory || !entry.name.startsWith("data_") || !entry.name.endsWith(".bin"))
            continue;
        char candidate_path[80];
        snprintf(candidate_path, sizeof(candidate_path), "%s/%s", ALM5A_DIRECTORY, entry.name.c_str());
        bool registered = false;
        for (size_t index = 0; index < registered_count; ++index) {
            if (strcmp(registered_paths[index], candidate_path) == 0) {
                registered = true;
                break;
            }
        }
        if (!registered) {
            Serial.printf("[SD-VERIFY] FALLO: archivo parcial/sin manifiesto: %s (%llu bytes).\n",
                          candidate_path, static_cast<unsigned long long>(entry.size_bytes));
            all_passed = false;
        }
    }
    if (checked == 0) {
        if (data_files != 0)
            Serial.printf("[SD-VERIFY] FALLO: se encontraron %u archivo(s) de datos sin registro persistente verificable.\n",
                          static_cast<unsigned>(data_files));
        Serial.println("[SD-VERIFY] RESULTADO: FAIL; no hay registros persistentes ALM5A para verificar.");
    } else {
        Serial.printf("[SD-VERIFY] RESULTADO: %s; registros revisados=%u.\n",
                      all_passed ? "PASS" : "FAIL", static_cast<unsigned>(checked));
    }
}
