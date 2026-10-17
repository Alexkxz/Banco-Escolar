#pragma once

#include <FS.h>
#include <stddef.h>
#include <stdint.h>

enum class BankLittleFsPathStatus : uint8_t {
    PRESENT,
    MISSING,
    ERROR
};

class BankLittleFS : public fs::FS {
public:
    BankLittleFS();

    bool mount(bool read_only, const char *base_path, const char *partition_label,
               bool grow_on_mount = false);
    bool unmount();
    bool remountReadOnly();
    // The only writable remount allowed while OTA maintenance is active.
    // It pins grow_on_mount=false and is used to verify the normal mount
    // before confirming a pending image. No school writes are allowed then.
    bool remountReadWriteForBootValidation();
    bool isMounted() const;
    bool isReadOnly() const;
    BankLittleFsPathStatus pathStatus(const char *path) const;
    bool format();
    size_t totalBytes();
    size_t usedBytes();

private:
    bool mountInternal(bool read_only, const char *base_path, const char *partition_label,
                       bool grow_on_mount, bool boot_validation_probe);
    const char *partition_label_ = nullptr;
    const char *base_path_ = nullptr;
    bool mounted_ = false;
    bool read_only_ = false;
};

extern BankLittleFS bank_littlefs;
