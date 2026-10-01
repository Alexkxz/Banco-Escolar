#include "sd_manager.h"

// Safe placeholder until the physical microSD is installed and its board
// interface is explicitly enabled. No filesystem library or GPIO is touched.
SDManager sd_manager;

void SDManager::begin()
{
    state_ = SdState::SD_NOT_PRESENT;
}

bool SDManager::mount()
{
    // Mounting is intentionally unavailable in this phase.
    state_ = SdState::SD_NOT_PRESENT;
    return false;
}

void SDManager::unmount()
{
    state_ = SdState::SD_NOT_PRESENT;
}

SdState SDManager::state() const
{
    return state_;
}

bool SDManager::isReady() const
{
    return state_ == SdState::SD_READY;
}

size_t SDManager::totalBytes() const
{
    return 0;
}

size_t SDManager::usedBytes() const
{
    return 0;
}

size_t SDManager::freeBytes() const
{
    return 0;
}
