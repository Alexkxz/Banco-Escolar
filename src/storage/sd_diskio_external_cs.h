#pragma once

#include <Arduino.h>
#include <SPI.h>
#include "sd_defines.h"
#include "sd_external_cs.h"

uint8_t sdcard_init_external_cs(SPIClass *spi, int hz,
                                SdExternalChipSelect::WriteCallback cs_callback,
                                void *cs_context);
uint8_t sdcard_uninit(uint8_t pdrv);
bool sdcard_mount(uint8_t pdrv, const char *path, uint8_t max_files, bool format_if_empty);
uint8_t sdcard_unmount(uint8_t pdrv);
sdcard_type_t sdcard_type(uint8_t pdrv);
uint32_t sdcard_num_sectors(uint8_t pdrv);
uint32_t sdcard_sector_size(uint8_t pdrv);
bool sd_read_raw(uint8_t pdrv, uint8_t *buffer, uint32_t sector);
bool sd_write_raw(uint8_t pdrv, uint8_t *buffer, uint32_t sector);
