#include "ota_lab_bootstrap.h"

#include "ota_config.h"

#if BANCO_OTA_LAB_BUILD

#if BANCO_OTA_ENABLED || BANCO_OTA_LAB_INSTALLER_BUILD
#error "OTA1 candidate must be isolated from production OTA and installer firmware"
#endif

#include <sdkconfig.h>
#if !CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
#error "OTA-LAB candidate requires bootloader application rollback"
#endif

#include <Arduino.h>
#include <esp_image_format.h>
#include <esp_ota_ops.h>

#include <cstring>

#include "../storage/ota_maintenance_gate.h"

namespace {
constexpr uint32_t OTA0_ADDRESS = 0x00010000;
constexpr uint32_t OTA1_ADDRESS = 0x00650000;
constexpr uint32_t OTA_SLOT_SIZE = 0x00640000;

static_assert(sizeof(BANCO_OTA_LAB_BUILD_ID) > 1,
              "The isolated OTA1 candidate must have a build identifier");
static_assert(sizeof(BANCO_OTA_LAB_BUILD_VERSION) > 1,
              "The isolated OTA1 candidate must have a signed manifest version marker");

[[noreturn]] void failClosed(const char *reason)
{
    ota_maintenance_active.store(true);
    Serial.printf("[OTA-LAB-CANDIDATE] RECHAZADO: %s; mantenimiento activo\n", reason);
    for (;;) delay(1000);
}

bool isAppPartition(const esp_partition_t *partition, esp_partition_subtype_t subtype,
                    uint32_t address)
{
    return partition != nullptr && partition->type == ESP_PARTITION_TYPE_APP &&
           partition->subtype == subtype && partition->address == address &&
           partition->size == OTA_SLOT_SIZE;
}
}

void ota_lab_bootstrap_before_services(bool otaManagerReady, bool managerDetectedPending)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    const esp_partition_t *ota0 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
    const esp_partition_t *ota1 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr);
    if (!isAppPartition(ota0, ESP_PARTITION_SUBTYPE_APP_OTA_0, OTA0_ADDRESS) ||
        !isAppPartition(ota1, ESP_PARTITION_SUBTYPE_APP_OTA_1, OTA1_ADDRESS) ||
        !isAppPartition(running, ESP_PARTITION_SUBTYPE_APP_OTA_1, OTA1_ADDRESS) ||
        boot == nullptr || boot->address != running->address ||
        strcmp(BANCO_OTA_LAB_BUILD_ID, "OTA-ARQ.2-CANDIDATE") != 0 ||
        strcmp(BANCO_OTA_LAB_BUILD_VERSION, "1.2.4-lab") != 0)
        failClosed("la imagen no es la candidata esperada ejecutándose desde ota_1");

    esp_image_metadata_t metadata = {};
    const esp_partition_pos_t position = {running->address, running->size};
    if (esp_image_verify(ESP_IMAGE_VERIFY, &position, &metadata) != ESP_OK ||
        metadata.image_len == 0 || metadata.image_len > running->size)
        failClosed("la imagen de ota_1 no pasó la verificación de estructura");

    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK || !otaManagerReady)
        failClosed("estado OTA desconocido");
    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        ota_maintenance_active.store(true);
        if (!managerDetectedPending)
            failClosed("el gestor no reconoce PENDING_VERIFY; validación manual bloqueada");
        Serial.printf("[OTA-LAB-CANDIDATE] %s versión=%s pendiente; continúan las verificaciones de arranque\n",
                      BANCO_OTA_LAB_BUILD_ID, BANCO_OTA_LAB_BUILD_VERSION);
        return;
    }
    if (state == ESP_OTA_IMG_VALID && !managerDetectedPending) {
        Serial.printf("[OTA-LAB-CANDIDATE] %s versión=%s ya confirmada; sin selección de slot\n",
                      BANCO_OTA_LAB_BUILD_ID, BANCO_OTA_LAB_BUILD_VERSION);
        return;
    }
    failClosed("estado NEW/INVALID/ABORTED/UNDEFINED inesperado; el candidato no selecciona particiones");
}

#else

void ota_lab_bootstrap_before_services(bool, bool) {}

#endif
