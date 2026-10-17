#pragma once

// OTA stays unavailable until a release build supplies all trust and identity
// settings through a private, locally maintained config header/build step.
#ifndef BANCO_OTA_ENABLED
#define BANCO_OTA_ENABLED 0
#endif

#ifndef BANCO_OTA_CURRENT_VERSION
#define BANCO_OTA_CURRENT_VERSION ""
#endif

#ifndef BANCO_OTA_SERVER_ROOT_CA_PEM
#define BANCO_OTA_SERVER_ROOT_CA_PEM ""
#endif

#ifndef BANCO_OTA_FIRMWARE_PUBLIC_KEY_PEM
#define BANCO_OTA_FIRMWARE_PUBLIC_KEY_PEM ""
#endif

#ifndef BANCO_OTA_DEVICE_MODEL
#define BANCO_OTA_DEVICE_MODEL "WAVESHARE_ESP32_S3_TOUCH_LCD_7_REV1_2"
#endif

// Aggregate compatibility version for persistent NVS/LittleFS schemas.
// Releases must keep this unchanged while rollback to the prior app is possible.
#ifndef BANCO_OTA_DATA_SCHEMA_VERSION
#define BANCO_OTA_DATA_SCHEMA_VERSION 1
#endif

