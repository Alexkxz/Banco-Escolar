#!/usr/bin/env python3
"""Source-level regression checks for the external SD chip-select path."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DRIVER = (ROOT / "src/storage/sd_diskio_external_cs.cpp").read_text(encoding="utf-8")
MANAGER = (ROOT / "src/storage/sd_manager.cpp").read_text(encoding="utf-8")
SELECTOR = (ROOT / "src/storage/sd_external_cs.h").read_text(encoding="utf-8")
PATHS = (ROOT / "src/storage/sd_path.h").read_text(encoding="utf-8")
HEADER = (ROOT / "src/storage/sd_manager.h").read_text(encoding="utf-8")


def body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    raise AssertionError(f"unclosed function: {signature}")


select = body(DRIVER, "bool sdSelectCard(uint8_t pdrv)")
deselect = body(DRIVER, "void sdDeselectCard(uint8_t pdrv)")
init = body(DRIVER, "uint8_t sdcard_init_external_cs(")
manager_begin = body(MANAGER, "bool SDManager::begin(")
initialize_bus = body(MANAGER, "bool SDManager::initializeBus(")
release_bus = body(MANAGER, "bool SDManager::releaseBus(")
cs_callback = body(MANAGER, "bool set_external_sd_cs(")
disk_write = body(DRIVER, "DRESULT ff_sd_write(")
write_file = body(MANAGER, "SdFileResult SDManager::writeFile(const char *path, const uint8_t *data")
read_file = body(MANAGER, "SdFileResult SDManager::readFile(")
exists_file = body(MANAGER, "SdFileResult SDManager::fileExists(")
size_file = body(MANAGER, "SdFileResult SDManager::fileSize(")
list_directory = body(MANAGER, "SdFileResult SDManager::listDirectory(")
create_directory = body(MANAGER, "SdFileResult SDManager::createDirectory(")
remove_file = body(MANAGER, "SdFileResult SDManager::removeFile(")
begin = body(MANAGER, "bool SDManager::begin(")
unmount = body(MANAGER, "bool SDManager::unmount(")

assert "digitalPinToGPIONumber" not in DRIVER
assert "pinMode(" not in DRIVER and "digitalWrite(" not in DRIVER
assert "sdcard_init(" not in MANAGER and "SD_SPI_CS_GPIO" not in MANAGER
assert "card->chip_select.configure(cs_callback, cs_context)" in init
assert "callback_(context_, true)" in body(SELECTOR, "bool select()")
assert "callback_(context_, false)" in body(SELECTOR, "bool deselect()")
assert select.index("chip_select.select()") < select.index("sdWait(pdrv, 500)")
assert "sdDeselectCard(pdrv);" in select[select.index("if (!s)") :]
assert "card->chip_select.deselect()" in deselect and "card->cs_error = true" in deselect
assert "set_external_sd_cs, expander_" in initialize_bus
assert "expander_->digitalWrite(SD_CS_EXIO, HIGH)" in initialize_bus
assert "expander_->digitalWrite(SD_CS_EXIO, HIGH)" in release_bus
assert "selected ? LOW : HIGH" in cs_callback
assert "SD_FORMAT_IF_EMPTY = false" in MANAGER
assert "sdWriteSectors(" in disk_write and "sdWriteSector(" in disk_write
assert "card->cs_error" in disk_write and "RES_NOTRDY" in disk_write
assert "sdcard_mount(drive_, SD_MOUNT_PATH, SD_MAX_OPEN_FILES, SD_FORMAT_IF_EMPTY)" in MANAGER
assert "SD_FORMAT_IF_EMPTY = false" in MANAGER
assert "listRoot()" in begin and "Montaje FAT persistente listo" in begin
assert "unmount()" not in begin[begin.index("Montaje FAT persistente listo") :]
assert "sdcard_unmount(drive_)" in unmount and "mounted_ = false" in unmount
assert "SdFileResult fileExists(const char *path" in HEADER
assert "SdFileResult fileSize(const char *path" in HEADER
assert "readFile(const char *path" in HEADER
assert "listDirectory(const char *path" in HEADER
assert "createDirectory(const char *path" in HEADER
assert "writeFile(const char *path" in HEADER
assert "removeFile(const char *path" in HEADER
assert "path[0] == '/'" in PATHS and "path[0] == '\\\\'" in PATHS
assert "path[length - 1] == '.'" in PATHS and "SdPathError::INVALID_COMPONENT" in PATHS
assert "SdPathError::RESERVED_SUFFIX" in PATHS
assert "validateAndBuildPath(path, full_path)" in write_file
assert "isReady()" in write_file and "recoverWrite(full_path)" in write_file
assert write_file.index("fflush(file)") < write_file.index("fsync(fileno(file))")
assert write_file.index("fsync(fileno(file))") < write_file.index("close_file(file")
assert write_file.index("rename(full_path.c_str(), backup.c_str())") < write_file.index("rename(temporary.c_str(), full_path.c_str())")
assert "rename(backup.c_str(), full_path.c_str())" in write_file
assert "unlink(backup.c_str())" in write_file
assert "recoverWrite(full_path)" in read_file
assert "close_file(file, result" in read_file
assert "fread(buffer" in read_file and "SD_MAX_BUFFERED_READ_BYTES" in MANAGER
assert "recoverWrite(full_path)" in exists_file and "errno == ENOENT" in exists_file
assert "recoverWrite(full_path)" in size_file and "S_ISDIR(info.st_mode)" in size_file
assert "opendir(full_path.c_str())" in list_directory and "closedir(directory)" in list_directory
assert "SD_MAX_DIRECTORY_ENTRIES" in list_directory and "sd_path_reserved_suffix" in list_directory
assert "mkdir(full_path.c_str()" in create_directory
assert "unlink(full_path.c_str())" in remove_file
assert "SdFileError::NO_SPACE" in MANAGER and "SdFileError::READ_FAILED" in MANAGER
assert 'validate_sd_path("../outside")' in PATHS
assert 'validate_sd_path("data/../outside")' in PATHS
assert 'validate_sd_path("data//file")' in PATHS

print("PASS: CS se activa antes de esperar/transmitir y se libera tras timeout/error.")
print("PASS: el driver no configura ni escribe un GPIO para CS (incluido GPIO 255).")
print("PASS: CH422G reutilizado; montaje persistente sin formato automático.")
print("PASS: API FAT con rutas relativas, lectura/listado y operaciones de archivo.")
print("PASS: escritura temporal con sincronización, respaldo y recuperación por ruta.")
