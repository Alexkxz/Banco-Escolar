#pragma once

#include <stdint.h>

enum class StorageSizeUnit : uint8_t {
    BYTES,
    KIBIBYTES,
    MEBIBYTES,
    GIBIBYTES
};

struct StorageSizeFormat {
    uint64_t value;
    StorageSizeUnit unit;
};

constexpr const char *storage_size_unit_label(StorageSizeUnit unit)
{
    switch (unit) {
        case StorageSizeUnit::BYTES: return "B";
        case StorageSizeUnit::KIBIBYTES: return "KiB";
        case StorageSizeUnit::MEBIBYTES: return "MiB";
        case StorageSizeUnit::GIBIBYTES: return "GiB";
    }
    return "";
}

constexpr bool storage_label_equals(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        if (*left++ != *right++) return false;
    }
    return *left == *right;
}

constexpr StorageSizeFormat storage_size_format(uint64_t bytes)
{
    constexpr uint64_t KIB = 1024ULL;
    constexpr uint64_t MIB = KIB * 1024ULL;
    constexpr uint64_t GIB = MIB * 1024ULL;
    if (bytes < KIB) return {bytes, StorageSizeUnit::BYTES};

    const uint64_t scale = bytes < MIB ? KIB : (bytes < GIB ? MIB : GIB);
    const StorageSizeUnit unit = bytes < MIB ? StorageSizeUnit::KIBIBYTES :
                                 (bytes < GIB ? StorageSizeUnit::MEBIBYTES :
                                                StorageSizeUnit::GIBIBYTES);
    const uint64_t whole = bytes / scale;
    const uint64_t remainder = bytes % scale;
    const uint64_t tenths = whole * 10ULL + (remainder * 10ULL + scale / 2ULL) / scale;
    return {tenths, unit};
}

constexpr uint64_t measured_capacity_bytes(uint64_t sectors, uint64_t bytes_per_sector)
{
    if (sectors == 0 || bytes_per_sector == 0 ||
        sectors > UINT64_MAX / bytes_per_sector) return 0;
    return sectors * bytes_per_sector;
}

// Compile-time regression checks for binary units, rounding, and 64-bit SD capacity.
static_assert(storage_size_format(1023).value == 1023 &&
              storage_size_format(1023).unit == StorageSizeUnit::BYTES,
              "Sub-KiB sizes must remain in bytes");
static_assert(storage_size_format(1024).value == 10 &&
              storage_size_format(1024).unit == StorageSizeUnit::KIBIBYTES,
              "1 KiB must format as 1.0 KiB");
static_assert(storage_size_format(1024ULL * 1024ULL).value == 10 &&
              storage_size_format(1024ULL * 1024ULL).unit == StorageSizeUnit::MEBIBYTES,
              "1 MiB must format as 1.0 MiB");
static_assert(storage_size_format(62568529920ULL).value == 583 &&
              storage_size_format(62568529920ULL).unit == StorageSizeUnit::GIBIBYTES,
              "Measured 64 GB-class media must format as 58.3 GiB");
static_assert(storage_size_format(UINT32_MAX).value == 40 &&
              storage_size_format(UINT32_MAX).unit == StorageSizeUnit::GIBIBYTES,
              "A 32-bit size_t ceiling must not be presented as 4096 MB");
static_assert(storage_label_equals(storage_size_unit_label(StorageSizeUnit::KIBIBYTES), "KiB") &&
              storage_label_equals(storage_size_unit_label(StorageSizeUnit::MEBIBYTES), "MiB") &&
              storage_label_equals(storage_size_unit_label(StorageSizeUnit::GIBIBYTES), "GiB"),
              "Storage format must use explicit binary unit labels");
static_assert(measured_capacity_bytes(122204160ULL, 512ULL) == 62568529920ULL,
              "SD capacity must use 64-bit sectors times sector size");
static_assert(measured_capacity_bytes(100ULL, 8ULL * 512ULL) == 409600ULL,
              "FAT volume capacity must use measured cluster and sector sizes");
static_assert(measured_capacity_bytes(UINT64_MAX, 2ULL) == 0,
              "Overflowing capacities must be treated as unavailable");
