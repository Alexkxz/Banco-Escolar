#include "sd_file_test.h"

#include <Arduino.h>
#include <string.h>
#include <vector>

#include "../storage/sd_manager.h"

namespace {
constexpr char TEST_DIRECTORY[] = "sd_diag_manual";
constexpr char TEST_FILE[] = "sd_diag_manual/payload.txt";
constexpr char INITIAL_CONTENT[] = "Banco Escolar SD diagnostic v1\n";
constexpr char REPLACEMENT_CONTENT[] = "Banco Escolar SD diagnostic replaced v2\n";

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
        Serial.println("[SD-DIAG] No se modificó ningún archivo de esa carpeta.");
        return;
    }

    bool file_created = false;
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
    }
    if (passed) passed = write_content(REPLACEMENT_CONTENT, true, "archivo reemplazado");
    if (passed) passed = check_content(REPLACEMENT_CONTENT, "lectura tras reemplazo");

    bool cleanup_ok = true;
    if (file_created) cleanup_ok = remove_test_file();
    if (!cleanup_ok) passed = false;

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
