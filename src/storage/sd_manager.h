#pragma once

#include <stddef.h>
#include <stdint.h>

namespace esp_expander {
class Base;
}

enum class SdState : uint8_t {
    SD_NOT_PRESENT,
    SD_MOUNTING,
    SD_READY,
    SD_ERROR
};

enum class SdBootStatus : uint8_t {
    CHECKING,
    AVAILABLE,
    UNAVAILABLE
};

constexpr SdBootStatus sd_boot_status(SdState state)
{
    switch (state) {
        case SdState::SD_MOUNTING: return SdBootStatus::CHECKING;
        case SdState::SD_READY: return SdBootStatus::AVAILABLE;
        case SdState::SD_NOT_PRESENT:
        case SdState::SD_ERROR: return SdBootStatus::UNAVAILABLE;
    }
    return SdBootStatus::UNAVAILABLE;
}

static_assert(sd_boot_status(SdState::SD_MOUNTING) == SdBootStatus::CHECKING,
              "The splash must show that the SD probe is running");
static_assert(sd_boot_status(SdState::SD_READY) == SdBootStatus::AVAILABLE,
              "A successful SD probe must be shown as available");
static_assert(sd_boot_status(SdState::SD_NOT_PRESENT) == SdBootStatus::UNAVAILABLE &&
              sd_boot_status(SdState::SD_ERROR) == SdBootStatus::UNAVAILABLE,
              "Missing or failed SD probes must be shown as unavailable");

enum class SdCardType : uint8_t {
    NONE,
    MMC,
    SDSC,
    SDHC,
    UNKNOWN
};

enum class SdError : uint8_t {
    NONE,
    EXPANDER_UNAVAILABLE,
    CHIP_SELECT_FAILED,
    SPI_DRIVER_FAILED,
    CARD_NOT_RESPONDING,
    FILESYSTEM_MOUNT_FAILED,
    ROOT_READ_FAILED
};

constexpr SdState sd_state_after_mount_failure(SdCardType card_type)
{
    // CARD_UNKNOWN means the SD driver could not get a valid response from a
    // card. A recognized card with a failed FAT mount is a different failure.
    return card_type == SdCardType::UNKNOWN ? SdState::SD_NOT_PRESENT : SdState::SD_ERROR;
}

static_assert(sd_state_after_mount_failure(SdCardType::UNKNOWN) == SdState::SD_NOT_PRESENT,
              "A non-responding SD card must be reported as not detected");
static_assert(sd_state_after_mount_failure(SdCardType::SDHC) == SdState::SD_ERROR &&
              sd_state_after_mount_failure(SdCardType::SDSC) == SdState::SD_ERROR &&
              sd_state_after_mount_failure(SdCardType::MMC) == SdState::SD_ERROR,
              "A recognized card with a failed filesystem mount must report an error");

class SDManager {
public:
    // Reuses the CH422G instance already initialized by Board::begin().
    bool begin(esp_expander::Base *expander);
    bool mount();
    bool unmount();

    SdState state() const;
    SdError error() const;
    bool isReady() const;
    SdCardType cardType() const;
    const char *cardTypeName() const;
    const char *errorName() const;
    uint64_t cardCapacityBytes() const;
    size_t rootEntryCount() const;
    bool filesystemStatsAvailable() const;
    uint64_t totalBytes() const;
    uint64_t usedBytes() const;
    uint64_t freeBytes() const;

private:
    bool listRoot();
    bool releaseBus();

    SdState state_ = SdState::SD_NOT_PRESENT;
    SdError error_ = SdError::NONE;
    SdCardType card_type_ = SdCardType::NONE;
    esp_expander::Base *expander_ = nullptr;
    uint8_t drive_ = 0xFF;
    bool spi_started_ = false;
    bool mounted_ = false;
    uint64_t card_capacity_bytes_ = 0;
    uint64_t total_bytes_ = 0;
    uint64_t used_bytes_ = 0;
    uint64_t free_bytes_ = 0;
    bool filesystem_stats_available_ = false;
    size_t root_entry_count_ = 0;
};

extern SDManager sd_manager;
