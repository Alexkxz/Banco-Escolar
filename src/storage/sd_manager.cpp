#include "sd_manager.h"
#include "storage_metrics.h"

#include <Arduino.h>
#include <SPI.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>

#include <esp_io_expander.hpp>
#include <ff.h>
#include <sd_defines.h>
#include "sd_diskio_external_cs.h"

namespace {
constexpr uint8_t SD_CS_EXIO = 4;
constexpr int SD_MOSI_GPIO = 11;
constexpr int SD_SCK_GPIO = 12;
constexpr int SD_MISO_GPIO = 13;
constexpr int SD_SPI_FREQUENCY_HZ = 4000000;
constexpr char SD_MOUNT_PATH[] = "/sdcard";
constexpr uint8_t SD_MAX_OPEN_FILES = 5;
constexpr bool SD_FORMAT_IF_EMPTY = false;

static_assert(!SD_FORMAT_IF_EMPTY, "SD diagnosis must never format a card");

bool set_external_sd_cs(void *context, bool selected)
{
    auto *expander = static_cast<esp_expander::Base *>(context);
    if (expander == nullptr) return false;
    return expander->digitalWrite(SD_CS_EXIO, selected ? LOW : HIGH);
}

SdCardType map_card_type(sdcard_type_t card_type)
{
    switch (card_type) {
        case CARD_NONE: return SdCardType::NONE;
        case CARD_MMC: return SdCardType::MMC;
        case CARD_SD: return SdCardType::SDSC;
        case CARD_SDHC: return SdCardType::SDHC;
        case CARD_UNKNOWN: return SdCardType::UNKNOWN;
    }
    return SdCardType::UNKNOWN;
}

} // namespace

SDManager sd_manager;

bool SDManager::begin(esp_expander::Base *expander)
{
    // A previous diagnostic should have closed all resources. Recover them if
    // begin() is called again, without changing the last useful status yet.
    if (drive_ != 0xFF || spi_started_) unmount();

    state_ = SdState::SD_MOUNTING;
    error_ = SdError::NONE;
    card_type_ = SdCardType::NONE;
    card_capacity_bytes_ = 0;
    total_bytes_ = 0;
    used_bytes_ = 0;
    free_bytes_ = 0;
    filesystem_stats_available_ = false;
    root_entry_count_ = 0;
    expander_ = expander;

    if (expander_ == nullptr) {
        state_ = SdState::SD_ERROR;
        error_ = SdError::EXPANDER_UNAVAILABLE;
        Serial.println("[MicroSD] Error: no está disponible el expansor CH422G de la placa");
        return false;
    }

    // Board::begin() already initialized this same CH422G instance and set
    // EXIO0-7 to outputs. Keep external CS inactive until the SPI driver
    // asserts it for each transaction.
    if (!expander_->digitalWrite(SD_CS_EXIO, HIGH)) {
        state_ = SdState::SD_ERROR;
        error_ = SdError::CHIP_SELECT_FAILED;
        Serial.println("[MicroSD] Error: no se pudo desactivar EXIO4 (SD_CS)");
        return false;
    }

    SPI.setHwCs(false);
    SPI.begin(SD_SCK_GPIO, SD_MISO_GPIO, SD_MOSI_GPIO);
    spi_started_ = true;
    drive_ = sdcard_init_external_cs(&SPI, SD_SPI_FREQUENCY_HZ,
                                     set_external_sd_cs, expander_);
    if (drive_ == 0xFF) {
        state_ = SdState::SD_ERROR;
        error_ = SdError::SPI_DRIVER_FAILED;
        Serial.println("[MicroSD] Error: no se pudo preparar el controlador SPI de la tarjeta");
        unmount();
        return false;
    }

    if (!mount()) {
        Serial.printf("[MicroSD] %s; tipo=%s; capacidad=%llu bytes\n",
                      errorName(), cardTypeName(),
                      static_cast<unsigned long long>(card_capacity_bytes_));
        unmount();
        return false;
    }

    Serial.printf("[MicroSD] Montada para lectura: tipo=%s; capacidad=%llu bytes\n",
                  cardTypeName(), static_cast<unsigned long long>(card_capacity_bytes_));
    const bool root_read = listRoot();
    const bool closed = unmount();
    if (!root_read) return false;
    if (!closed) return false;

    state_ = SdState::SD_READY;
    error_ = SdError::NONE;
    Serial.println("[MicroSD] Diagnóstico de solo lectura finalizado; tarjeta desmontada");
    return true;
}

bool SDManager::mount()
{
    if (drive_ == 0xFF) {
        state_ = SdState::SD_ERROR;
        error_ = SdError::SPI_DRIVER_FAILED;
        return false;
    }

    state_ = SdState::SD_MOUNTING;
    // Explicit false is required here: a missing/unsupported FAT volume must
    // fail untouched, never trigger f_mkfs.
    if (!sdcard_mount(drive_, SD_MOUNT_PATH, SD_MAX_OPEN_FILES, SD_FORMAT_IF_EMPTY)) {
        card_type_ = map_card_type(sdcard_type(drive_));
        card_capacity_bytes_ = measured_capacity_bytes(sdcard_num_sectors(drive_),
                                                       sdcard_sector_size(drive_));
        state_ = sd_state_after_mount_failure(card_type_);
        error_ = state_ == SdState::SD_NOT_PRESENT ? SdError::CARD_NOT_RESPONDING :
                                                     SdError::FILESYSTEM_MOUNT_FAILED;
        return false;
    }

    mounted_ = true;
    card_type_ = map_card_type(sdcard_type(drive_));
    card_capacity_bytes_ = measured_capacity_bytes(sdcard_num_sectors(drive_),
                                                   sdcard_sector_size(drive_));
    if (card_type_ == SdCardType::NONE || card_type_ == SdCardType::UNKNOWN ||
        card_capacity_bytes_ == 0) {
        state_ = SdState::SD_ERROR;
        error_ = SdError::CARD_NOT_RESPONDING;
        return false;
    }

    char volume[3] = {static_cast<char>('0' + drive_), ':', '\0'};
    FATFS *filesystem = nullptr;
    DWORD free_clusters = 0;
    if (f_getfree(volume, &free_clusters, &filesystem) == FR_OK && filesystem != nullptr) {
        const uint64_t cluster_count = filesystem->n_fatent > 2U
            ? static_cast<uint64_t>(filesystem->n_fatent - 2U) : 0;
#if _MAX_SS != 512
        const uint64_t bytes_per_sector = filesystem->ssize;
#else
        constexpr uint64_t bytes_per_sector = 512;
#endif
        const uint64_t cluster_bytes = measured_capacity_bytes(filesystem->csize,
                                                               bytes_per_sector);
        const uint64_t filesystem_total = measured_capacity_bytes(cluster_count, cluster_bytes);
        const uint64_t filesystem_free = measured_capacity_bytes(free_clusters, cluster_bytes);
        if (filesystem_total > 0 && filesystem_free <= filesystem_total) {
            total_bytes_ = filesystem_total;
            free_bytes_ = filesystem_free;
            used_bytes_ = total_bytes_ - free_bytes_;
            filesystem_stats_available_ = true;
        }
    }
    state_ = SdState::SD_READY;
    error_ = SdError::NONE;
    return true;
}

bool SDManager::listRoot()
{
    DIR *root = opendir(SD_MOUNT_PATH);
    if (root == nullptr) {
        state_ = SdState::SD_ERROR;
        error_ = SdError::ROOT_READ_FAILED;
        Serial.printf("[MicroSD] Error al abrir la raíz %s: errno=%d\n", SD_MOUNT_PATH, errno);
        return false;
    }

    Serial.printf("[MicroSD] Contenido de %s:\n", SD_MOUNT_PATH);
    errno = 0;
    struct dirent *entry = nullptr;
    while ((entry = readdir(root)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        ++root_entry_count_;
        Serial.printf("[MicroSD]   %s%s\n",
                      entry->d_type == DT_DIR ? "[DIR]  " : "[FILE] ", entry->d_name);
    }
    const int read_error = errno;
    closedir(root);
    if (read_error != 0) {
        state_ = SdState::SD_ERROR;
        error_ = SdError::ROOT_READ_FAILED;
        Serial.printf("[MicroSD] Error al leer la raíz: errno=%d\n", read_error);
        return false;
    }
    if (root_entry_count_ == 0) Serial.println("[MicroSD]   (raíz vacía)");
    Serial.printf("[MicroSD] Elementos en la raíz: %u\n",
                  static_cast<unsigned>(root_entry_count_));
    return true;
}

bool SDManager::unmount()
{
    bool closed = true;
    if (drive_ != 0xFF) {
        if (mounted_ && sdcard_unmount(drive_) != 0) {
            closed = false;
            Serial.println("[MicroSD] Error al desmontar FAT");
        }
        mounted_ = false;
        if (sdcard_uninit(drive_) != 0) {
            closed = false;
            Serial.println("[MicroSD] Error al liberar el controlador de la tarjeta");
        }
        drive_ = 0xFF;
    }

    if (!releaseBus()) closed = false;
    if (!closed) {
        state_ = SdState::SD_ERROR;
        error_ = SdError::SPI_DRIVER_FAILED;
    }
    return closed;
}

bool SDManager::releaseBus()
{
    bool released = true;
    if (expander_ != nullptr && !expander_->digitalWrite(SD_CS_EXIO, HIGH)) {
        released = false;
        Serial.println("[MicroSD] Error al desactivar EXIO4 (SD_CS)");
    }
    if (spi_started_) {
        SPI.end();
        spi_started_ = false;
    }
    return released;
}

SdState SDManager::state() const
{
    return state_;
}

SdError SDManager::error() const
{
    return error_;
}

bool SDManager::isReady() const
{
    // READY means the read-only probe, FAT mount, and root listing succeeded;
    // the card is intentionally unmounted before begin() returns.
    return state_ == SdState::SD_READY;
}

SdCardType SDManager::cardType() const
{
    return card_type_;
}

const char *SDManager::cardTypeName() const
{
    switch (card_type_) {
        case SdCardType::NONE: return "desconocido";
        case SdCardType::MMC: return "MMC";
        case SdCardType::SDSC: return "SDSC";
        case SdCardType::SDHC: return "SDHC";
        case SdCardType::UNKNOWN: return "sin respuesta";
    }
    return "desconocido";
}

const char *SDManager::errorName() const
{
    switch (error_) {
        case SdError::NONE: return "sin error";
        case SdError::EXPANDER_UNAVAILABLE: return "CH422G no disponible";
        case SdError::CHIP_SELECT_FAILED: return "falló EXIO4/SD_CS";
        case SdError::SPI_DRIVER_FAILED: return "falló el controlador SPI";
        case SdError::CARD_NOT_RESPONDING: return "tarjeta ausente o sin respuesta";
        case SdError::FILESYSTEM_MOUNT_FAILED: return "tarjeta detectada, montaje FAT fallido (sin formatear)";
        case SdError::ROOT_READ_FAILED: return "no se pudo leer la raíz";
    }
    return "fallo de microSD";
}

uint64_t SDManager::cardCapacityBytes() const
{
    return card_capacity_bytes_;
}

size_t SDManager::rootEntryCount() const
{
    return root_entry_count_;
}

bool SDManager::filesystemStatsAvailable() const
{
    return filesystem_stats_available_;
}

uint64_t SDManager::totalBytes() const
{
    return total_bytes_;
}

uint64_t SDManager::usedBytes() const
{
    return used_bytes_;
}

uint64_t SDManager::freeBytes() const
{
    return free_bytes_;
}
