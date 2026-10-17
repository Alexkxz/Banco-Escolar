#include "ota_lab_installer.h"
#include "ota_lab_origin_policy.h"

#include "ota_config.h"

#if BANCO_OTA_LAB_INSTALLER_BUILD

#if BANCO_OTA_ENABLED || BANCO_OTA_LAB_BUILD
#error "The installer must be isolated from production Wi-Fi OTA and OTA1 candidate builds"
#endif

#include <sdkconfig.h>
#if !CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
#error "The installer requires bootloader application rollback"
#endif

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_app_desc.h>
#include <esp_image_format.h>
#include <esp_ota_ops.h>
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <mbedtls/pk.h>

#include <cstdio>
#include <cstring>

#include "../storage/bank_littlefs.h"
#include "../storage/ota_maintenance_gate.h"
#include "../storage/storage_manager.h"

namespace {
static_assert(ota_lab_origin_policy::kNew == ESP_OTA_IMG_NEW);
static_assert(ota_lab_origin_policy::kPendingVerify == ESP_OTA_IMG_PENDING_VERIFY);
static_assert(ota_lab_origin_policy::kValid == ESP_OTA_IMG_VALID);
static_assert(ota_lab_origin_policy::kInvalid == ESP_OTA_IMG_INVALID);
static_assert(ota_lab_origin_policy::kAborted == ESP_OTA_IMG_ABORTED);
static_assert(ota_lab_origin_policy::kUndefined == ESP_OTA_IMG_UNDEFINED);

constexpr uint32_t OTA0_ADDRESS = 0x00010000;
constexpr uint32_t OTA1_ADDRESS = 0x00650000;
constexpr uint32_t OTA_SLOT_SIZE = 0x00640000;
constexpr uint32_t OTADATA_ADDRESS = 0x0000E000;
constexpr uint32_t OTADATA_SIZE = 0x00002000;
constexpr uint32_t FILESYSTEM_ADDRESS = 0x00C90000;
constexpr uint32_t FILESYSTEM_SIZE = 0x00360000;
constexpr size_t MANIFEST_LIMIT = 2048;
constexpr size_t INSTALLER_IDENTITY_LIMIT = 1024;
constexpr size_t INSTALLER_IDENTITY_HEADER_SIZE = 10;
constexpr size_t IO_CHUNK = 1024;
constexpr uint32_t SERIAL_TIMEOUT_MS = 5000;
constexpr uint32_t IMAGE_TIMEOUT_MS = 15000;
constexpr char OPERATOR_COMMAND[] = "INSTALL OTA1 OTA-ARQ.2";
constexpr char CANDIDATE_VERSION[] = "1.2.4-lab";
constexpr char CANONICAL_PREFIX[] = "BANCO-OTA-LAB-MANIFEST-V1\n";
constexpr char INSTALLER_IDENTITY_MAGIC[] = "BANCOID1";
constexpr char INSTALLER_IDENTITY_FORMAT[] = "BANCO-OTA-LAB-INSTALLER-V1";
constexpr char INSTALLER_IDENTITY_VERSION[] = "OTA-ARQ.2";
constexpr char INSTALLER_IDENTITY_BUILD_ID[] = "OTA-ARQ.2-INSTALLER";
constexpr char INSTALLER_IDENTITY_PREFIX[] = "BANCO-OTA-LAB-INSTALLER-V1\n";
constexpr char HEX_DIGITS[] = "0123456789abcdef";

static_assert(sizeof(BANCO_OTA_LAB_PUBLIC_KEY_PEM) > 1,
              "Installer build requires the explicitly lab-only P-256 public key");

[[noreturn]] void failClosed(const char *reason)
{
    ota_maintenance_active.store(true);
    Serial.printf("[OTA-LAB-INSTALLER] RECHAZADO: %s; mantenimiento activo\n", reason);
    for (;;) delay(1000);
}

bool isPartition(const esp_partition_t *p, esp_partition_subtype_t subtype,
                 uint32_t address, uint32_t size)
{
    return p != nullptr && p->type == ESP_PARTITION_TYPE_APP && p->subtype == subtype &&
           p->address == address && p->size == size;
}

bool isDataPartition(const esp_partition_t *p, esp_partition_subtype_t subtype,
                     uint32_t address, uint32_t size)
{
    return p != nullptr && p->type == ESP_PARTITION_TYPE_DATA && p->subtype == subtype &&
           p->address == address && p->size == size;
}

bool samePartition(const esp_partition_t *a, const esp_partition_t *b)
{
    return a != nullptr && b != nullptr && a->type == b->type && a->subtype == b->subtype &&
           a->address == b->address && a->size == b->size;
}

bool sha256Partition(const esp_partition_t *partition, size_t length, uint8_t out[32])
{
    if (partition == nullptr || out == nullptr || length == 0 || length > partition->size) return false;
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    bool ok = mbedtls_sha256_starts(&ctx, 0) == 0;
    uint8_t buffer[IO_CHUNK];
    for (size_t offset = 0; ok && offset < length;) {
        const size_t amount = (length - offset < sizeof(buffer)) ? length - offset : sizeof(buffer);
        if (esp_partition_read(partition, offset, buffer, amount) != ESP_OK ||
            mbedtls_sha256_update(&ctx, buffer, amount) != 0) ok = false;
        offset += amount;
    }
    if (ok) ok = mbedtls_sha256_finish(&ctx, out) == 0;
    mbedtls_sha256_free(&ctx);
    return ok;
}

bool digestToHex(const uint8_t digest[32], char output[65])
{
    if (digest == nullptr || output == nullptr) return false;
    for (size_t i = 0; i < 32; ++i) {
        output[i * 2] = HEX_DIGITS[digest[i] >> 4];
        output[i * 2 + 1] = HEX_DIGITS[digest[i] & 0x0f];
    }
    output[64] = '\0';
    return true;
}

bool isLowerSha256(const char *value)
{
    if (value == nullptr || strlen(value) != 64) return false;
    for (const char *p = value; *p; ++p) {
        if (!( (*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') )) return false;
    }
    return true;
}

bool signatureValid(const char *canonical, size_t canonicalLength, const char *encoded)
{
    if (canonical == nullptr || encoded == nullptr || *encoded == '\0') return false;
    uint8_t signature[128] = {};
    size_t signatureLength = 0;
    if (mbedtls_base64_decode(signature, sizeof(signature), &signatureLength,
            reinterpret_cast<const unsigned char *>(encoded), strlen(encoded)) != 0 ||
        signatureLength == 0) return false;

    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
    bool valid = false;
    if (mbedtls_pk_parse_public_key(&key,
            reinterpret_cast<const unsigned char *>(BANCO_OTA_LAB_PUBLIC_KEY_PEM),
            strlen(BANCO_OTA_LAB_PUBLIC_KEY_PEM) + 1) == 0 &&
        (mbedtls_pk_get_type(&key) == MBEDTLS_PK_ECKEY ||
         mbedtls_pk_get_type(&key) == MBEDTLS_PK_ECDSA) &&
        mbedtls_pk_ec(key)->MBEDTLS_PRIVATE(grp).id == MBEDTLS_ECP_DP_SECP256R1) {
        uint8_t digest[32] = {};
        const mbedtls_md_info_t *sha = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        if (sha != nullptr && mbedtls_md(sha,
                reinterpret_cast<const unsigned char *>(canonical), canonicalLength, digest) == 0) {
            valid = mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, digest, sizeof(digest),
                                      signature, signatureLength) == 0;
        }
    }
    mbedtls_pk_free(&key);
    return valid;
}

bool readExact(uint8_t *out, size_t length, uint32_t timeoutMs)
{
    if (out == nullptr) return false;
    const uint32_t start = millis();
    size_t read = 0;
    while (read < length && millis() - start < timeoutMs) {
        const int value = Serial.read();
        if (value >= 0) out[read++] = static_cast<uint8_t>(value);
        else delay(1);
    }
    return read == length;
}

void waitForOperatorCommand()
{
    Serial.printf("[OTA-LAB-INSTALLER] listo; envíe exactamente: %s\n", OPERATOR_COMMAND);
    char line[sizeof(OPERATOR_COMMAND) + 8] = {};
    size_t length = 0;
    bool overflow = false;
    for (;;) {
        while (Serial.available() > 0) {
            const int ch = Serial.read();
            if (ch < 0 || ch == '\r') continue;
            if (ch == '\n') {
                line[length] = '\0';
                const bool approved = !overflow && strcmp(line, OPERATOR_COMMAND) == 0;
                memset(line, 0, sizeof(line));
                length = 0;
                overflow = false;
                if (approved) return;
                Serial.println("[OTA-LAB-INSTALLER] orden rechazada; flash intacta");
            } else if (length + 1 < sizeof(line)) line[length++] = static_cast<char>(ch);
            else overflow = true;
        }
        delay(2);
    }
}

bool systemGeometryIsExact(const esp_partition_t *ota0, const esp_partition_t *ota1,
                           const char *&failureReason)
{
    const esp_partition_t *nvs = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "nvs");
    const esp_partition_t *otadata = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, "otadata");
    const esp_partition_t *filesystem = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
    const esp_partition_t *coredump = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, "coredump");

    if (!isDataPartition(nvs, ESP_PARTITION_SUBTYPE_DATA_NVS, 0x00009000, 0x00005000)) {
        failureReason = "geometría de partición nvs inesperada";
        return false;
    }
    if (!isDataPartition(otadata, ESP_PARTITION_SUBTYPE_DATA_OTA, OTADATA_ADDRESS, OTADATA_SIZE)) {
        failureReason = "geometría de partición otadata inesperada";
        return false;
    }
    if (!isPartition(ota0, ESP_PARTITION_SUBTYPE_APP_OTA_0, OTA0_ADDRESS, OTA_SLOT_SIZE)) {
        failureReason = "geometría de partición ota_0 inesperada";
        return false;
    }
    if (!isPartition(ota1, ESP_PARTITION_SUBTYPE_APP_OTA_1, OTA1_ADDRESS, OTA_SLOT_SIZE)) {
        failureReason = "geometría de partición ota_1 inesperada";
        return false;
    }
    if (!isDataPartition(filesystem, ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
                         FILESYSTEM_ADDRESS, FILESYSTEM_SIZE)) {
        failureReason = "geometría de partición spiffs inesperada";
        return false;
    }
    if (!isDataPartition(coredump, ESP_PARTITION_SUBTYPE_DATA_COREDUMP,
                         0x00FF0000, 0x00010000)) {
        failureReason = "geometría de partición coredump inesperada";
        return false;
    }

    size_t partitionCount = 0;
    esp_partition_iterator_t iterator = esp_partition_find(
        ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);
    while (iterator != nullptr) {
        ++partitionCount;
        iterator = esp_partition_next(iterator);
    }
    if (partitionCount != 6) {
        failureReason = "cantidad de particiones distinta de la tabla aprobada";
        return false;
    }
    return true;
}

bool approvedInstallerIdentityMatches(const esp_partition_t *partition, size_t imageLength,
                                      const uint8_t imageDigest[32], const char *&failureReason)
{
    if (partition == nullptr || imageDigest == nullptr || imageLength > partition->size ||
        partition->size - imageLength < INSTALLER_IDENTITY_HEADER_SIZE) {
        failureReason = "espacio insuficiente para la identidad firmada";
        return false;
    }

    uint8_t header[INSTALLER_IDENTITY_HEADER_SIZE] = {};
    if (esp_partition_read(partition, imageLength, header, sizeof(header)) != ESP_OK) {
        failureReason = "no se pudo leer el encabezado de identidad";
        return false;
    }
    if (memcmp(header, INSTALLER_IDENTITY_MAGIC, sizeof(INSTALLER_IDENTITY_MAGIC) - 1) != 0) {
        failureReason = "falta el trailer de identidad aprobado";
        return false;
    }

    const size_t manifestLength = static_cast<size_t>(header[8]) |
                                  (static_cast<size_t>(header[9]) << 8);
    if (manifestLength == 0 || manifestLength > INSTALLER_IDENTITY_LIMIT ||
        manifestLength > partition->size - imageLength - INSTALLER_IDENTITY_HEADER_SIZE) {
        failureReason = "longitud del manifiesto de identidad inválida";
        return false;
    }

    uint8_t manifestBytes[INSTALLER_IDENTITY_LIMIT + 1] = {};
    if (esp_partition_read(partition, imageLength + INSTALLER_IDENTITY_HEADER_SIZE,
                           manifestBytes, manifestLength) != ESP_OK) {
        failureReason = "no se pudo leer el manifiesto de identidad";
        return false;
    }
    manifestBytes[manifestLength] = '\0';

    StaticJsonDocument<INSTALLER_IDENTITY_LIMIT> document;
    if (deserializeJson(document, reinterpret_cast<const char *>(manifestBytes), manifestLength,
                        DeserializationOption::NestingLimit(2)) != DeserializationError::Ok) {
        failureReason = "manifiesto de identidad JSON inválido";
        return false;
    }
    const JsonObjectConst object = document.as<JsonObjectConst>();
    if (object.isNull() || object.size() != 7 ||
        !object["format"].is<const char *>() || !object["model"].is<const char *>() ||
        !object["version"].is<const char *>() || !object["build_id"].is<const char *>() ||
        !object["image_size"].is<uint32_t>() || !object["sha256"].is<const char *>() ||
        !object["signature"].is<const char *>()) {
        failureReason = "campos del manifiesto de identidad inválidos";
        return false;
    }

    const char *format = object["format"].as<const char *>();
    const char *model = object["model"].as<const char *>();
    const char *version = object["version"].as<const char *>();
    const char *buildId = object["build_id"].as<const char *>();
    const char *expectedSha = object["sha256"].as<const char *>();
    const char *signature = object["signature"].as<const char *>();
    if (format == nullptr || model == nullptr || version == nullptr || buildId == nullptr ||
        expectedSha == nullptr || signature == nullptr ||
        strcmp(format, INSTALLER_IDENTITY_FORMAT) != 0 ||
        strcmp(model, BANCO_OTA_DEVICE_MODEL) != 0 ||
        strcmp(version, INSTALLER_IDENTITY_VERSION) != 0 ||
        strcmp(buildId, INSTALLER_IDENTITY_BUILD_ID) != 0 ||
        object["image_size"].as<uint32_t>() != imageLength || !isLowerSha256(expectedSha)) {
        failureReason = "modelo, versión, build o tamaño de identidad no aprobados";
        return false;
    }

    char actualSha[65] = {};
    if (!digestToHex(imageDigest, actualSha) || strcmp(actualSha, expectedSha) != 0) {
        failureReason = "SHA-256 de imagen no coincide con la identidad firmada";
        return false;
    }

    char canonical[320] = {};
    const int canonicalLength = snprintf(canonical, sizeof(canonical),
        "%s%s\n%s\n%s\n%lu\n%s", INSTALLER_IDENTITY_PREFIX, model, version, buildId,
        static_cast<unsigned long>(imageLength), expectedSha);
    if (canonicalLength <= 0 || static_cast<size_t>(canonicalLength) >= sizeof(canonical) ||
        !signatureValid(canonical, static_cast<size_t>(canonicalLength), signature)) {
        failureReason = "firma P-256 de identidad inválida";
        return false;
    }
    return true;
}

struct Manifest {
    char model[64] = {};
    char version[32] = {};
    char target[16] = {};
    char sha256[65] = {};
    char buildId[40] = {};
    char signature[180] = {};
    uint32_t targetOffset = 0;
    uint32_t targetSize = 0;
    uint32_t imageSize = 0;
    uint16_t dataSchema = 0;
    bool rollbackCompatible = false;
};

bool parseAndVerifyManifest(const uint8_t *bytes, size_t length,
                            const esp_partition_t *target, Manifest &manifest)
{
    if (bytes == nullptr || length == 0 || length > MANIFEST_LIMIT || target == nullptr) return false;
    static StaticJsonDocument<MANIFEST_LIMIT> document;
    document.clear();
    if (deserializeJson(document, reinterpret_cast<const char *>(bytes), length,
                        DeserializationOption::NestingLimit(2)) != DeserializationError::Ok) return false;
    const JsonObjectConst object = document.as<JsonObjectConst>();
    if (object.isNull() || object.size() != 11 ||
        !object["model"].is<const char *>() || !object["version"].is<const char *>() ||
        !object["target"].is<const char *>() || !object["offset"].is<uint32_t>() ||
        !object["slot_size"].is<uint32_t>() || !object["size"].is<uint32_t>() ||
        !object["sha256"].is<const char *>() || !object["build_id"].is<const char *>() ||
        !object["data_schema"].is<uint16_t>() ||
        !object["rollback_compatible"].is<bool>() || !object["signature"].is<const char *>()) return false;

    const char *model = object["model"].as<const char *>();
    const char *version = object["version"].as<const char *>();
    const char *targetName = object["target"].as<const char *>();
    const char *sha = object["sha256"].as<const char *>();
    const char *buildId = object["build_id"].as<const char *>();
    const char *signature = object["signature"].as<const char *>();
    if (model == nullptr || version == nullptr || targetName == nullptr || sha == nullptr ||
        buildId == nullptr || signature == nullptr || strlen(model) >= sizeof(manifest.model) ||
        strlen(version) >= sizeof(manifest.version) || strlen(targetName) >= sizeof(manifest.target) ||
        strlen(sha) >= sizeof(manifest.sha256) || strlen(buildId) >= sizeof(manifest.buildId) ||
        strlen(signature) >= sizeof(manifest.signature)) return false;

    strlcpy(manifest.model, model, sizeof(manifest.model));
    strlcpy(manifest.version, version, sizeof(manifest.version));
    strlcpy(manifest.target, targetName, sizeof(manifest.target));
    strlcpy(manifest.sha256, sha, sizeof(manifest.sha256));
    strlcpy(manifest.buildId, buildId, sizeof(manifest.buildId));
    strlcpy(manifest.signature, signature, sizeof(manifest.signature));
    manifest.targetOffset = object["offset"].as<uint32_t>();
    manifest.targetSize = object["slot_size"].as<uint32_t>();
    manifest.imageSize = object["size"].as<uint32_t>();
    manifest.dataSchema = object["data_schema"].as<uint16_t>();
    manifest.rollbackCompatible = object["rollback_compatible"].as<bool>();

    if (strcmp(manifest.model, BANCO_OTA_DEVICE_MODEL) != 0 ||
        strcmp(manifest.target, "ota_1") != 0 ||
        manifest.targetOffset != target->address || manifest.targetSize != target->size ||
        manifest.imageSize == 0 || manifest.imageSize > target->size ||
        !isLowerSha256(manifest.sha256) || manifest.dataSchema != BANCO_OTA_DATA_SCHEMA_VERSION ||
        !manifest.rollbackCompatible || manifest.version[0] == '\0' ||
        strcmp(manifest.buildId, "OTA-ARQ.2-CANDIDATE") != 0 ||
        strcmp(manifest.version, CANDIDATE_VERSION) != 0) return false;

    char canonical[512] = {};
    const int canonicalLength = snprintf(canonical, sizeof(canonical),
        "%s%s\n%s\n%s\n%08lx\n%08lx\n%lu\n%s\n%s\n%u\n%s",
        CANONICAL_PREFIX, manifest.model, manifest.version, manifest.target,
        static_cast<unsigned long>(manifest.targetOffset),
        static_cast<unsigned long>(manifest.targetSize),
        static_cast<unsigned long>(manifest.imageSize), manifest.sha256, manifest.buildId,
        static_cast<unsigned>(manifest.dataSchema), manifest.rollbackCompatible ? "true" : "false");
    return canonicalLength > 0 && static_cast<size_t>(canonicalLength) < sizeof(canonical) &&
           signatureValid(canonical, static_cast<size_t>(canonicalLength), manifest.signature);
}

bool hashMatches(const esp_partition_t *partition, size_t length, const char *expected)
{
    uint8_t digest[32] = {};
    char actual[65] = {};
    return sha256Partition(partition, length, digest) && digestToHex(digest, actual) &&
           strcmp(actual, expected) == 0;
}

bool partitionContains(const esp_partition_t *partition, size_t length, const char *needle)
{
    if (partition == nullptr || length == 0 || length > partition->size || needle == nullptr || *needle == '\0') return false;
    const size_t needleLength = strlen(needle);
    uint8_t buffer[IO_CHUNK];
    uint8_t overlap[64] = {};
    if (needleLength >= sizeof(overlap)) return false;
    size_t overlapLength = 0;
    for (size_t offset = 0; offset < length;) {
        const size_t amount = (length - offset < sizeof(buffer)) ? length - offset : sizeof(buffer);
        if (esp_partition_read(partition, offset, buffer, amount) != ESP_OK) return false;
        uint8_t scan[sizeof(overlap) + sizeof(buffer)];
        memcpy(scan, overlap, overlapLength);
        memcpy(scan + overlapLength, buffer, amount);
        const size_t scanLength = overlapLength + amount;
        for (size_t i = 0; i + needleLength <= scanLength; ++i) {
            if (memcmp(scan + i, needle, needleLength) == 0) return true;
        }
        overlapLength = needleLength - 1;
        if (overlapLength > scanLength) overlapLength = scanLength;
        memcpy(overlap, scan + scanLength - overlapLength, overlapLength);
        offset += amount;
    }
    return false;
}

void installOneCandidate(const esp_partition_t *ota0, const esp_partition_t *ota1,
                         size_t ota0ImageLength, const uint8_t ota0Digest[32])
{
    uint8_t prefix[2] = {};
    if (!readExact(prefix, sizeof(prefix), SERIAL_TIMEOUT_MS)) failClosed("longitud de manifiesto no recibida");
    const size_t manifestLength = static_cast<size_t>(prefix[0]) |
                                  (static_cast<size_t>(prefix[1]) << 8);
    if (manifestLength == 0 || manifestLength > MANIFEST_LIMIT) failClosed("longitud de manifiesto inválida");
    uint8_t manifestBytes[MANIFEST_LIMIT + 1] = {};
    if (!readExact(manifestBytes, manifestLength, SERIAL_TIMEOUT_MS)) failClosed("manifiesto incompleto");
    Manifest manifest;
    if (!parseAndVerifyManifest(manifestBytes, manifestLength, ota1, manifest))
        failClosed("manifiesto, firma, destino o compatibilidad rechazados");

    esp_ota_handle_t handle = 0;
    if (esp_ota_begin(ota1, manifest.imageSize, &handle) != ESP_OK)
        failClosed("esp_ota_begin rechazó ota_1");
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    bool ok = mbedtls_sha256_starts(&hash, 0) == 0;
    uint8_t buffer[IO_CHUNK];
    size_t received = 0;
    while (ok && received < manifest.imageSize) {
        const size_t amount = (manifest.imageSize - received < sizeof(buffer))
            ? manifest.imageSize - received : sizeof(buffer);
        if (!readExact(buffer, amount, IMAGE_TIMEOUT_MS) ||
            esp_ota_write(handle, buffer, amount) != ESP_OK ||
            mbedtls_sha256_update(&hash, buffer, amount) != 0) {
            ok = false;
            break;
        }
        received += amount;
        Serial.printf("[OTA-LAB-INSTALLER] recepción %lu/%lu\n",
                      static_cast<unsigned long>(received),
                      static_cast<unsigned long>(manifest.imageSize));
    }
    uint8_t streamedDigest[32] = {};
    if (ok) ok = received == manifest.imageSize && mbedtls_sha256_finish(&hash, streamedDigest) == 0;
    mbedtls_sha256_free(&hash);
    if (!ok) {
        (void)esp_ota_abort(handle);
        failClosed("transferencia interrumpida o escritura/hash de flujo fallidos; ota_0 sigue seleccionado");
    }
    char streamedHex[65] = {};
    digestToHex(streamedDigest, streamedHex);
    if (strcmp(streamedHex, manifest.sha256) != 0) {
        (void)esp_ota_abort(handle);
        failClosed("SHA-256 del flujo distinto al manifiesto firmado");
    }
    if (esp_ota_end(handle) != ESP_OK) failClosed("esp_ota_end rechazó la imagen ESP");

    // Authenticate the bytes actually persisted in ota_1, not just the host
    // stream or the image's internal ESP checksum.
    if (!hashMatches(ota1, manifest.imageSize, manifest.sha256))
        failClosed("SHA-256 de lectura posterior en ota_1 no coincide");
    esp_image_metadata_t metadata = {};
    const esp_partition_pos_t position = {ota1->address, ota1->size};
    if (esp_image_verify(ESP_IMAGE_VERIFY, &position, &metadata) != ESP_OK ||
        metadata.image_len != manifest.imageSize || metadata.image_len > ota1->size)
        failClosed("estructura o longitud final de imagen ESP no coincide");
    esp_app_desc_t description = {};
    if (esp_ota_get_partition_description(ota1, &description) != ESP_OK ||
        description.magic_word != ESP_APP_DESC_MAGIC_WORD ||
        !partitionContains(ota1, metadata.image_len, manifest.buildId) ||
        !partitionContains(ota1, metadata.image_len, manifest.version))
        failClosed("identidad/versión declarada no coincide con los bytes de la imagen");

    uint8_t ota0After[32] = {};
    if (!sha256Partition(ota0, ota0ImageLength, ota0After) ||
        memcmp(ota0After, ota0Digest, sizeof(ota0After)) != 0)
        failClosed("la imagen activa ota_0 cambió durante la instalación");

    // Exactly one official selector call, and only after every byte-level
    // check succeeded. IDF writes the alternate otadata sector atomically.
    if (esp_ota_set_boot_partition(ota1) != ESP_OK ||
        !samePartition(esp_ota_get_boot_partition(), ota1))
        failClosed("selección oficial de ota_1 falló; ota_0 permanece intacta");
    Serial.println("[OTA-LAB-INSTALLER] ota_1 verificada y seleccionada; reinicio solicitado");
    delay(100);
    esp_restart();
    failClosed("esp_restart regresó inesperadamente");
}

bool validateRunningOta0(const esp_partition_t *&running, const esp_partition_t *&ota0,
                         const esp_partition_t *&ota1, size_t &imageLength, uint8_t digest[32],
                         const char *&failureReason)
{
    failureReason = "validación de origen fallida";
    running = esp_ota_get_running_partition();
    ota0 = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                    ESP_PARTITION_SUBTYPE_APP_OTA_0, "ota_0");
    ota1 = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                    ESP_PARTITION_SUBTYPE_APP_OTA_1, "ota_1");
    if (!systemGeometryIsExact(ota0, ota1, failureReason)) {
        if (failureReason == nullptr) failureReason = "tabla de particiones inesperada";
        return false;
    }
    if (!samePartition(running, ota0)) {
        failureReason = "la partición en ejecución no es ota_0";
        return false;
    }
    if (!samePartition(esp_ota_get_boot_partition(), ota0)) {
        failureReason = "la selección de arranque no es ota_0";
        return false;
    }

    esp_image_metadata_t metadata = {};
    const esp_partition_pos_t position = {running->address, running->size};
    if (esp_image_verify(ESP_IMAGE_VERIFY, &position, &metadata) != ESP_OK ||
        metadata.image_len == 0 || metadata.image_len > running->size) {
        failureReason = "imagen ESP inválida o longitud fuera de ota_0";
        return false;
    }
    imageLength = metadata.image_len;
    if (!sha256Partition(running, imageLength, digest)) {
        failureReason = "no se pudo calcular SHA-256 de la imagen en ejecución";
        return false;
    }
    if (!approvedInstallerIdentityMatches(running, imageLength, digest, failureReason)) {
        if (failureReason == nullptr) failureReason = "identidad firmada del instalador no coincide";
        return false;
    }

    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    const esp_err_t stateError = esp_ota_get_state_partition(running, &state);
    if (stateError != ESP_OK) {
        failureReason = "error al consultar estado OTA de ota_0";
        return false;
    }
    if (!ota_lab_origin_policy::mayUseRunningImage(true, static_cast<uint32_t>(state), true)) {
        switch (state) {
        case ESP_OTA_IMG_NEW: failureReason = "estado OTA NEW no permitido"; break;
        case ESP_OTA_IMG_PENDING_VERIFY: failureReason = "estado OTA PENDING_VERIFY no permitido"; break;
        case ESP_OTA_IMG_INVALID: failureReason = "estado OTA INVALID no permitido"; break;
        case ESP_OTA_IMG_ABORTED: failureReason = "estado OTA ABORTED no permitido"; break;
        default: failureReason = "estado OTA desconocido no permitido"; break;
        }
        return false;
    }
    return true;
}

bool storageSafeReadOnly()
{
    if (!bank_littlefs.isMounted() && !storage_manager.begin()) return false;
    return storage_manager.isReady() && !storage_manager.hasIncompleteTransactions() &&
           bank_littlefs.isMounted() && bank_littlefs.isReadOnly();
}
}

void ota_lab_installer_before_services(bool otaManagerReady)
{
    ota_maintenance_active.store(true);
    if (!otaManagerReady) failClosed("gestor OTA/rollback no está listo");

    const esp_partition_t *running = nullptr;
    const esp_partition_t *ota0 = nullptr;
    const esp_partition_t *ota1 = nullptr;
    size_t ota0ImageLength = 0;
    uint8_t ota0Digest[32] = {};
    const char *validationFailure = nullptr;
    if (!validateRunningOta0(running, ota0, ota1, ota0ImageLength, ota0Digest,
                             validationFailure))
        failClosed(validationFailure != nullptr ? validationFailure : "validación de origen fallida");
    if (!storageSafeReadOnly()) failClosed("LittleFS, journals o datos escolares no son seguros");

    Serial.printf("[OTA-LAB-INSTALLER] origen ota_0 verificado (%lu bytes); destino exclusivo ota_1; Wi-Fi OTA desactivado\n",
                  static_cast<unsigned long>(ota0ImageLength));
    waitForOperatorCommand();

    // Consume the operator's one-shot approval and recheck all safety
    // preconditions immediately before accepting the signed package.
    size_t checkedLength = 0;
    uint8_t checkedDigest[32] = {};
    const esp_partition_t *checkedRunning = nullptr;
    const esp_partition_t *checkedOta0 = nullptr;
    const esp_partition_t *checkedOta1 = nullptr;
    validationFailure = nullptr;
    if (!validateRunningOta0(checkedRunning, checkedOta0, checkedOta1, checkedLength,
                             checkedDigest, validationFailure) ||
        !samePartition(checkedRunning, running) || !samePartition(checkedOta0, ota0) ||
        !samePartition(checkedOta1, ota1) || checkedLength != ota0ImageLength ||
        memcmp(checkedDigest, ota0Digest, sizeof(ota0Digest)) != 0 || !storageSafeReadOnly())
        failClosed(validationFailure != nullptr ? validationFailure :
                   "precondiciones cambiaron tras la orden local");
    installOneCandidate(ota0, ota1, ota0ImageLength, ota0Digest);
}

#else

void ota_lab_installer_before_services(bool) {}

#endif
