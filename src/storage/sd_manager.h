#pragma once

#include <stddef.h>
#include <stdint.h>

// Future microSD wiring for the target board:
// GPIO11 = MOSI, GPIO12 = SCK, GPIO13 = MISO, EXIO4 = SD_CS (CH422G expander).
// The future card filesystem is FAT32. This phase performs no GPIO or SD I/O.
enum class SdState : uint8_t {
    SD_NOT_PRESENT,
    SD_MOUNTING,
    SD_READY,
    SD_ERROR
};

class SDManager {
public:
    void begin();
    bool mount();
    void unmount();

    SdState state() const;
    bool isReady() const;
    size_t totalBytes() const;
    size_t usedBytes() const;
    size_t freeBytes() const;

private:
    SdState state_ = SdState::SD_NOT_PRESENT;
};

extern SDManager sd_manager;
