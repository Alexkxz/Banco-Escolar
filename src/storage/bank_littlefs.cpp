#include "bank_littlefs.h"
#include "ota_maintenance_gate.h"

#include <vfs_api.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

extern "C" {
#include <esp_littlefs.h>
}

namespace {
class BankLittleFSVfsImpl : public VFSImpl {
public:
    FileImplPtr open(const char *path, const char *mode, const bool create) override
    {
        if (ota_maintenance_active.load() &&
            (create || mode == nullptr || strpbrk(mode, "wa+"))) return {};
        return VFSImpl::open(path, mode, create);
    }

    bool rename(const char *from, const char *to) override
    {
        return !ota_maintenance_active.load() && VFSImpl::rename(from, to);
    }

    bool remove(const char *path) override
    {
        return !ota_maintenance_active.load() && VFSImpl::remove(path);
    }

    bool mkdir(const char *path) override
    {
        return !ota_maintenance_active.load() && VFSImpl::mkdir(path);
    }

    bool rmdir(const char *path) override
    {
        return !ota_maintenance_active.load() && VFSImpl::rmdir(path);
    }
};
}

BankLittleFS bank_littlefs;

// The pre-confirmation writable probe relies on the audited v1.16.1 adapter:
// its mount path only scans metadata, and growth is a separate operation.
// Fail the build if the platform silently changes that contract version.
static_assert(ESP_LITTLEFS_VERSION_MAJOR == 1 &&
              ESP_LITTLEFS_VERSION_MINOR == 16 &&
              ESP_LITTLEFS_VERSION_PATCH == 1,
              "Re-audit esp_littlefs mount semantics before changing versions");

BankLittleFS::BankLittleFS() : fs::FS(fs::FSImplPtr(new BankLittleFSVfsImpl())) {}

bool BankLittleFS::mount(bool read_only, const char *base_path, const char *partition_label,
                         bool grow_on_mount)
{
    return mountInternal(read_only, base_path, partition_label, grow_on_mount, false);
}

bool BankLittleFS::mountInternal(bool read_only, const char *base_path,
                                 const char *partition_label, bool grow_on_mount,
                                 bool boot_validation_probe)
{
    if ((ota_maintenance_active.load() && !read_only && !boot_validation_probe) ||
        (boot_validation_probe &&
         (!ota_maintenance_active.load() || read_only || grow_on_mount))) return false;
    if (mounted_) return read_only_ == read_only;
    if (base_path == nullptr || partition_label == nullptr || (read_only && grow_on_mount) ||
        esp_littlefs_mounted(partition_label) ||
        (boot_validation_probe && !ota_maintenance_active.load())) return false;

    partition_label_ = partition_label;
    const esp_vfs_littlefs_conf_t config = {
        .base_path = base_path,
        .partition_label = partition_label,
        .partition = nullptr,
        .format_if_mount_failed = false,
        .read_only = read_only,
        .dont_mount = false,
        .grow_on_mount = grow_on_mount
    };
    if (esp_vfs_littlefs_register(&config) != ESP_OK) return false;

    _impl->mountpoint(base_path);
    base_path_ = base_path;
    mounted_ = true;
    read_only_ = read_only;
    return true;
}

bool BankLittleFS::unmount()
{
    if (!mounted_) return true;
    if (esp_vfs_littlefs_unregister(partition_label_) != ESP_OK) return false;
    _impl->mountpoint(nullptr);
    mounted_ = false;
    read_only_ = false;
    return true;
}

bool BankLittleFS::remountReadOnly()
{
    if (!mounted_ || read_only_) return mounted_ && read_only_;
    if (!unmount()) return false;
    return mount(true, base_path_, partition_label_, false);
}

bool BankLittleFS::remountReadWriteForBootValidation()
{
    if (!ota_maintenance_active.load() || !mounted_ || !read_only_) return false;
    if (!unmount()) return false;
    // For esp_littlefs 1.16.1, lfs_mount scans metadata and updates only RAM
    // state. grow_on_mount invokes lfs_fs_grow separately and writes the
    // superblock, so this probe always disables it.
    if (mountInternal(false, base_path_, partition_label_, false, true)) return true;
    // Restore read access if the normal writable mount cannot be established.
    // The caller leaves maintenance active and rejects the candidate image.
    (void)mountInternal(true, base_path_, partition_label_, false, false);
    return false;
}

bool BankLittleFS::isMounted() const
{
    return mounted_;
}

bool BankLittleFS::isReadOnly() const
{
    return mounted_ && read_only_;
}

BankLittleFsPathStatus BankLittleFS::pathStatus(const char *path) const
{
    if (!mounted_ || base_path_ == nullptr || path == nullptr) return BankLittleFsPathStatus::ERROR;

    char full_path[256];
    const int length = snprintf(full_path, sizeof(full_path), "%s%s", base_path_, path);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(full_path))
        return BankLittleFsPathStatus::ERROR;

    struct stat status = {};
    if (::stat(full_path, &status) == 0) return BankLittleFsPathStatus::PRESENT;
    return errno == ENOENT ? BankLittleFsPathStatus::MISSING : BankLittleFsPathStatus::ERROR;
}

bool BankLittleFS::format()
{
    if (mounted_ || read_only_ || partition_label_ == nullptr) return false;
    return esp_littlefs_format(partition_label_) == ESP_OK;
}

size_t BankLittleFS::totalBytes()
{
    size_t total = 0;
    size_t used = 0;
    return mounted_ && esp_littlefs_info(partition_label_, &total, &used) == ESP_OK ? total : 0;
}

size_t BankLittleFS::usedBytes()
{
    size_t total = 0;
    size_t used = 0;
    return mounted_ && esp_littlefs_info(partition_label_, &total, &used) == ESP_OK ? used : 0;
}
