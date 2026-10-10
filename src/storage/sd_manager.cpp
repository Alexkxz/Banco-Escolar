#include "sd_manager.h"
#include "storage_metrics.h"

#include <Arduino.h>
#include <SPI.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include <esp_heap_caps.h>

#include <esp_io_expander.hpp>
#include <ff.h>
#include <sd_defines.h>
#include "sd_diskio_external_cs.h"
#include "sd_path.h"

namespace {
constexpr uint8_t SD_CS_EXIO = 4;
constexpr int SD_MOSI_GPIO = 11;
constexpr int SD_SCK_GPIO = 12;
constexpr int SD_MISO_GPIO = 13;
constexpr int SD_SPI_FREQUENCY_HZ = 4000000;
constexpr char SD_MOUNT_PATH[] = "/sdcard";
constexpr uint8_t SD_MAX_OPEN_FILES = 5;
constexpr bool SD_FORMAT_IF_EMPTY = false;
constexpr uint32_t SD_MAX_BUFFERED_READ_BYTES = 256U * 1024U;
constexpr size_t SD_MAX_DIRECTORY_ENTRIES = 128;

static_assert(!SD_FORMAT_IF_EMPTY, "SD diagnosis must never format a card");

bool set_external_sd_cs(void *context, bool selected)
{
    auto *expander = static_cast<esp_expander::Base *>(context);
    if (expander == nullptr) return false;
    return expander->digitalWrite(SD_CS_EXIO, selected ? LOW : HIGH);
}

SdFileResult file_error(SdFileError error, int system_error = 0)
{
    return {error, system_error};
}

SdFileError map_io_error(int value, SdFileError fallback)
{
    if (value == ENOSPC || value == EDQUOT) return SdFileError::NO_SPACE;
    if (value == ENOENT) return SdFileError::NOT_FOUND;
    if (value == EISDIR) return SdFileError::IS_DIRECTORY;
    if (value == ENOTDIR) return SdFileError::NOT_DIRECTORY;
    if (value == EEXIST) return SdFileError::ALREADY_EXISTS;
    return fallback;
}

bool path_suffix(String &path, const char *suffix)
{
    const size_t old_length = path.length();
    const size_t suffix_length = strlen(suffix);
    if (old_length + suffix_length >= 256) return false;
    return path.concat(suffix);
}

bool close_file(FILE *file, SdFileResult &result, SdFileError close_fallback)
{
    if (file == nullptr) return true;
    errno = 0;
    if (fclose(file) == 0) return true;
    if (result.ok()) result = file_error(map_io_error(errno, close_fallback), errno);
    return false;
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

const char *sd_file_error_name(SdFileError error)
{
    switch (error) {
        case SdFileError::NONE: return "sin error";
        case SdFileError::NOT_MOUNTED: return "microSD no montada";
        case SdFileError::INVALID_PATH: return "ruta inválida o fuera de /sdcard";
        case SdFileError::NOT_FOUND: return "archivo no encontrado";
        case SdFileError::ALREADY_EXISTS: return "el destino ya existe";
        case SdFileError::NOT_DIRECTORY: return "la ruta no es un directorio";
        case SdFileError::IS_DIRECTORY: return "la ruta es un directorio";
        case SdFileError::RESOURCE_LIMIT: return "el archivo excede el límite de recursos";
        case SdFileError::NO_SPACE: return "espacio insuficiente en microSD";
        case SdFileError::READ_FAILED: return "falló la lectura";
        case SdFileError::WRITE_FAILED: return "falló la escritura";
        case SdFileError::CLOSE_FAILED: return "falló el cierre del archivo";
        case SdFileError::LIST_FAILED: return "falló la lectura del directorio";
        case SdFileError::CREATE_DIRECTORY_FAILED: return "falló la creación del directorio";
        case SdFileError::REMOVE_FAILED: return "falló la eliminación";
        case SdFileError::RENAME_FAILED: return "falló el renombrado";
    }
    return "error de archivo microSD";
}

bool SDManager::begin(esp_expander::Base *expander)
{
    // Recover resources if the board manager is initialized again.
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

    if (!initializeBus()) return false;

    if (!mount()) {
        Serial.printf("[MicroSD] %s; tipo=%s; capacidad=%llu bytes\n",
                      errorName(), cardTypeName(),
                      static_cast<unsigned long long>(card_capacity_bytes_));
        unmount();
        return false;
    }

    Serial.printf("[MicroSD] Montada: tipo=%s; capacidad=%llu bytes\n",
                  cardTypeName(), static_cast<unsigned long long>(card_capacity_bytes_));
    if (!listRoot()) {
        unmount();
        return false;
    }

    state_ = SdState::SD_READY;
    error_ = SdError::NONE;
    Serial.println("[MicroSD] Montaje FAT persistente listo");
    return true;
}

bool SDManager::initializeBus()
{
    if (drive_ != 0xFF) return true;
    if (expander_ == nullptr) {
        state_ = SdState::SD_ERROR;
        error_ = SdError::EXPANDER_UNAVAILABLE;
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
    return true;
}

bool SDManager::mount()
{
    if (mounted_) {
        state_ = SdState::SD_READY;
        error_ = SdError::NONE;
        return true;
    }
    if (!initializeBus()) return false;
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
    const bool was_mounted = mounted_;
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
        error_ = SdError::UNMOUNT_FAILED;
    } else if (was_mounted && state_ != SdState::SD_ERROR) {
        state_ = SdState::SD_UNMOUNTED;
        error_ = SdError::NONE;
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
    return mounted_ && state_ == SdState::SD_READY;
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
        case SdError::UNMOUNT_FAILED: return "falló el desmontaje de FAT o SPI";
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

SdFileResult SDManager::validateAndBuildPath(const char *path, String &full_path) const
{
    if (validate_sd_path(path) != SdPathError::NONE) return file_error(SdFileError::INVALID_PATH);
    full_path = SD_MOUNT_PATH;
    if (!full_path.concat("/") || !full_path.concat(path))
        return file_error(SdFileError::INVALID_PATH);
    return {};
}

SdFileResult SDManager::recoverWrite(const String &full_path)
{
    String temporary = full_path;
    String backup = full_path;
    if (!path_suffix(temporary, ".tmp") || !path_suffix(backup, ".bak"))
        return file_error(SdFileError::INVALID_PATH);

    struct stat target_info {};
    struct stat backup_info {};
    errno = 0;
    const bool target_exists = stat(full_path.c_str(), &target_info) == 0;
    const int target_errno = target_exists ? 0 : errno;
    errno = 0;
    const bool backup_exists = stat(backup.c_str(), &backup_info) == 0;
    const int backup_errno = backup_exists ? 0 : errno;
    if (!target_exists && target_errno != ENOENT)
        return file_error(map_io_error(target_errno, SdFileError::READ_FAILED), target_errno);
    if (!backup_exists && backup_errno != ENOENT)
        return file_error(map_io_error(backup_errno, SdFileError::READ_FAILED), backup_errno);
    if (backup_exists) {
        if (target_exists) {
            if (unlink(backup.c_str()) != 0)
                return file_error(map_io_error(errno, SdFileError::REMOVE_FAILED), errno);
        } else if (rename(backup.c_str(), full_path.c_str()) != 0) {
            return file_error(map_io_error(errno, SdFileError::RENAME_FAILED), errno);
        }
    }

    struct stat temporary_info {};
    errno = 0;
    if (stat(temporary.c_str(), &temporary_info) == 0) {
        errno = 0;
        if (unlink(temporary.c_str()) != 0)
            return file_error(map_io_error(errno, SdFileError::REMOVE_FAILED), errno);
    } else if (errno != ENOENT) {
        return file_error(map_io_error(errno, SdFileError::READ_FAILED), errno);
    }
    return {};
}

SdFileResult SDManager::fileExists(const char *path, bool &exists)
{
    exists = false;
    if (!isReady()) return file_error(SdFileError::NOT_MOUNTED);
    String full_path;
    SdFileResult result = validateAndBuildPath(path, full_path);
    if (!result.ok()) return result;
    result = recoverWrite(full_path);
    if (!result.ok()) return result;
    struct stat info {};
    errno = 0;
    if (stat(full_path.c_str(), &info) == 0) {
        exists = true;
        return {};
    }
    if (errno == ENOENT) return {};
    return file_error(map_io_error(errno, SdFileError::READ_FAILED), errno);
}

SdFileResult SDManager::fileSize(const char *path, uint64_t &size_bytes)
{
    size_bytes = 0;
    if (!isReady()) return file_error(SdFileError::NOT_MOUNTED);
    String full_path;
    SdFileResult result = validateAndBuildPath(path, full_path);
    if (!result.ok()) return result;
    result = recoverWrite(full_path);
    if (!result.ok()) return result;
    struct stat info {};
    errno = 0;
    if (stat(full_path.c_str(), &info) != 0)
        return file_error(map_io_error(errno, SdFileError::READ_FAILED), errno);
    if (S_ISDIR(info.st_mode)) return file_error(SdFileError::IS_DIRECTORY);
    if (info.st_size < 0) return file_error(SdFileError::READ_FAILED);
    size_bytes = static_cast<uint64_t>(info.st_size);
    return {};
}

SdFileResult SDManager::readFile(const char *path, std::vector<uint8_t> &contents)
{
    contents.clear();
    if (!isReady()) return file_error(SdFileError::NOT_MOUNTED);
    String full_path;
    SdFileResult result = validateAndBuildPath(path, full_path);
    if (!result.ok()) return result;
    result = recoverWrite(full_path);
    if (!result.ok()) return result;

    errno = 0;
    FILE *file = fopen(full_path.c_str(), "rb");
    if (file == nullptr) return file_error(map_io_error(errno, SdFileError::READ_FAILED), errno);
    result = {};
    struct stat info {};
    errno = 0;
    if (fstat(fileno(file), &info) != 0) {
        result = file_error(map_io_error(errno, SdFileError::READ_FAILED), errno);
    } else if (info.st_size < 0 || static_cast<uint64_t>(info.st_size) > SD_MAX_BUFFERED_READ_BYTES) {
        result = file_error(SdFileError::RESOURCE_LIMIT);
    } else if (static_cast<size_t>(info.st_size) > heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)) {
        result = file_error(SdFileError::RESOURCE_LIMIT, ENOMEM);
    } else {
        contents.reserve(static_cast<size_t>(info.st_size));
    }
    uint8_t buffer[512];
    for (; result.ok();) {
        errno = 0;
        const size_t count = fread(buffer, 1, sizeof(buffer), file);
        if (count > 0) contents.insert(contents.end(), buffer, buffer + count);
        if (count < sizeof(buffer)) {
            if (ferror(file)) result = file_error(map_io_error(errno, SdFileError::READ_FAILED), errno);
            break;
        }
    }
    close_file(file, result, SdFileError::CLOSE_FAILED);
    if (!result.ok()) contents.clear();
    return result;
}

SdFileResult SDManager::streamFile(const char *path, SdStreamStartCallback on_start,
                                   SdStreamCallback on_chunk, void *context, bool headers_only,
                                   uint64_t &file_size_bytes, uint64_t &bytes_streamed)
{
    file_size_bytes = 0;
    bytes_streamed = 0;
    if (!isReady()) return file_error(SdFileError::NOT_MOUNTED);
    if (on_start == nullptr || (!headers_only && on_chunk == nullptr))
        return file_error(SdFileError::READ_FAILED, EINVAL);
    String full_path;
    SdFileResult result = validateAndBuildPath(path, full_path);
    if (!result.ok()) return result;

    errno = 0;
    FILE *file = fopen(full_path.c_str(), "rb");
    if (file == nullptr) return file_error(map_io_error(errno, SdFileError::READ_FAILED), errno);
    struct stat info {};
    errno = 0;
    if (fstat(fileno(file), &info) != 0) {
        result = file_error(map_io_error(errno, SdFileError::READ_FAILED), errno);
    } else if (S_ISDIR(info.st_mode)) {
        result = file_error(SdFileError::IS_DIRECTORY);
    } else if (info.st_size < 0) {
        result = file_error(SdFileError::READ_FAILED);
    } else {
        file_size_bytes = static_cast<uint64_t>(info.st_size);
    }

    if (result.ok() && !on_start(context, file_size_bytes))
        result = file_error(SdFileError::RESOURCE_LIMIT);
    if (result.ok() && headers_only) {
        close_file(file, result, SdFileError::CLOSE_FAILED);
        return result;
    }

    uint8_t buffer[1024];
    while (result.ok()) {
        errno = 0;
        const size_t count = fread(buffer, 1, sizeof(buffer), file);
        if (count > 0) {
            if (!on_chunk(context, buffer, count)) {
                result = file_error(SdFileError::READ_FAILED);
                break;
            }
            bytes_streamed += count;
        }
        if (count < sizeof(buffer)) {
            if (ferror(file)) result = file_error(map_io_error(errno, SdFileError::READ_FAILED), errno);
            break;
        }
    }
    close_file(file, result, SdFileError::CLOSE_FAILED);
    if (result.ok() && bytes_streamed != static_cast<uint64_t>(info.st_size))
        result = file_error(SdFileError::READ_FAILED);
    return result;
}

SdFileResult SDManager::listDirectory(const char *path, std::vector<SdFileInfo> &entries)
{
    entries.clear();
    if (!isReady()) return file_error(SdFileError::NOT_MOUNTED);
    String full_path;
    if (path == nullptr || path[0] == '\0') full_path = SD_MOUNT_PATH;
    else {
        SdFileResult result = validateAndBuildPath(path, full_path);
        if (!result.ok()) return result;
    }

    errno = 0;
    DIR *directory = opendir(full_path.c_str());
    if (directory == nullptr) return file_error(map_io_error(errno, SdFileError::LIST_FAILED), errno);
    SdFileResult result;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(directory);
        if (entry == nullptr) {
            if (errno != 0) result = file_error(map_io_error(errno, SdFileError::LIST_FAILED), errno);
            break;
        }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        const size_t name_length = strlen(entry->d_name);
        if (sd_path_reserved_suffix(entry->d_name, name_length)) continue;
        if (entries.size() >= SD_MAX_DIRECTORY_ENTRIES) {
            result = file_error(SdFileError::RESOURCE_LIMIT);
            break;
        }

        SdFileInfo info;
        info.name = entry->d_name;
        String child = full_path;
        if (!child.endsWith("/") && !child.concat("/")) {
            result = file_error(SdFileError::LIST_FAILED, ENOMEM);
            break;
        }
        if (!child.concat(entry->d_name)) {
            result = file_error(SdFileError::LIST_FAILED, ENOMEM);
            break;
        }
        struct stat child_info {};
        errno = 0;
        if (stat(child.c_str(), &child_info) != 0) {
            result = file_error(map_io_error(errno, SdFileError::LIST_FAILED), errno);
            break;
        }
        info.is_directory = S_ISDIR(child_info.st_mode);
        info.size_bytes = info.is_directory || child_info.st_size < 0
            ? 0 : static_cast<uint64_t>(child_info.st_size);
        entries.push_back(info);
    }
    errno = 0;
    if (closedir(directory) != 0 && result.ok())
        result = file_error(map_io_error(errno, SdFileError::LIST_FAILED), errno);
    if (!result.ok()) entries.clear();
    return result;
}

SdFileResult SDManager::createDirectory(const char *path)
{
    if (!isReady()) return file_error(SdFileError::NOT_MOUNTED);
    String full_path;
    SdFileResult result = validateAndBuildPath(path, full_path);
    if (!result.ok()) return result;
    struct stat info {};
    errno = 0;
    if (stat(full_path.c_str(), &info) == 0) return file_error(SdFileError::ALREADY_EXISTS, EEXIST);
    if (errno != ENOENT) return file_error(map_io_error(errno, SdFileError::CREATE_DIRECTORY_FAILED), errno);
    errno = 0;
    if (mkdir(full_path.c_str(), 0777) != 0)
        return file_error(map_io_error(errno, SdFileError::CREATE_DIRECTORY_FAILED), errno);
    return {};
}

SdFileResult SDManager::writeFile(const char *path, const uint8_t *data, size_t length,
                                 bool replace)
{
    if (!isReady()) return file_error(SdFileError::NOT_MOUNTED);
    if (data == nullptr && length != 0) return file_error(SdFileError::WRITE_FAILED, EINVAL);
    String full_path;
    SdFileResult result = validateAndBuildPath(path, full_path);
    if (!result.ok()) return result;
    result = recoverWrite(full_path);
    if (!result.ok()) return result;

    struct stat target_info {};
    errno = 0;
    const bool target_exists = stat(full_path.c_str(), &target_info) == 0;
    if (!target_exists && errno != ENOENT)
        return file_error(map_io_error(errno, SdFileError::WRITE_FAILED), errno);
    if (target_exists && S_ISDIR(target_info.st_mode)) return file_error(SdFileError::IS_DIRECTORY);
    if (target_exists && !replace) return file_error(SdFileError::ALREADY_EXISTS, EEXIST);

    String temporary = full_path;
    String backup = full_path;
    if (!path_suffix(temporary, ".tmp") || !path_suffix(backup, ".bak"))
        return file_error(SdFileError::INVALID_PATH);
    errno = 0;
    FILE *file = fopen(temporary.c_str(), "wb");
    if (file == nullptr) return file_error(map_io_error(errno, SdFileError::WRITE_FAILED), errno);

    result = {};
    size_t written = 0;
    while (written < length) {
        errno = 0;
        const size_t count = fwrite(data + written, 1, length - written, file);
        written += count;
        if (count == 0) {
            result = file_error(map_io_error(errno, SdFileError::WRITE_FAILED), errno);
            break;
        }
    }
    errno = 0;
    if (result.ok() && fflush(file) != 0)
        result = file_error(map_io_error(errno, SdFileError::WRITE_FAILED), errno);
    if (result.ok() && fsync(fileno(file)) != 0)
        result = file_error(map_io_error(errno, SdFileError::WRITE_FAILED), errno);
    close_file(file, result, SdFileError::CLOSE_FAILED);
    if (!result.ok()) {
        unlink(temporary.c_str());
        return result;
    }

    if (target_exists && rename(full_path.c_str(), backup.c_str()) != 0) {
        const int saved_errno = errno;
        unlink(temporary.c_str());
        return file_error(map_io_error(saved_errno, SdFileError::RENAME_FAILED), saved_errno);
    }
    if (rename(temporary.c_str(), full_path.c_str()) != 0) {
        const int saved_errno = errno;
        if (target_exists) rename(backup.c_str(), full_path.c_str());
        unlink(temporary.c_str());
        return file_error(map_io_error(saved_errno, SdFileError::RENAME_FAILED), saved_errno);
    }
    if (target_exists && unlink(backup.c_str()) != 0)
        return file_error(map_io_error(errno, SdFileError::REMOVE_FAILED), errno);
    return {};
}

SdFileResult SDManager::writeFile(const char *path, const String &contents, bool replace)
{
    return writeFile(path, reinterpret_cast<const uint8_t *>(contents.c_str()),
                     contents.length(), replace);
}

SdFileResult SDManager::removeFile(const char *path)
{
    if (!isReady()) return file_error(SdFileError::NOT_MOUNTED);
    String full_path;
    SdFileResult result = validateAndBuildPath(path, full_path);
    if (!result.ok()) return result;
    result = recoverWrite(full_path);
    if (!result.ok()) return result;
    struct stat info {};
    errno = 0;
    if (stat(full_path.c_str(), &info) != 0)
        return file_error(map_io_error(errno, SdFileError::REMOVE_FAILED), errno);
    if (S_ISDIR(info.st_mode)) return file_error(SdFileError::IS_DIRECTORY);
    errno = 0;
    if (unlink(full_path.c_str()) != 0)
        return file_error(map_io_error(errno, SdFileError::REMOVE_FAILED), errno);
    return {};
}
