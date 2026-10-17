/* Private implementation details for ESP_IO_Expander drivers. */
#pragma once

#include "../src/port/esp_io_expander.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/portmacro.h"

typedef enum {
    ESP_IO_EXPANDER_LIFECYCLE_ACTIVE = 0,
    ESP_IO_EXPANDER_LIFECYCLE_CLOSING,
} esp_io_expander_lifecycle_state_t;

/* This definition is intentionally not part of the public API. In particular,
 * callers must not invoke internal callbacks (including the destructor).
 */
struct esp_io_expander_s {
    SemaphoreHandle_t operation_mutex;
    SemaphoreHandle_t lifecycle_idle;
    portMUX_TYPE lifecycle_lock;
    bool lifecycle_managed;
    esp_io_expander_lifecycle_state_t lifecycle_state;
    uint32_t admitted_operations;

    esp_err_t (*read_input_reg)(esp_io_expander_handle_t handle, uint32_t *value);
    esp_err_t (*write_output_reg)(esp_io_expander_handle_t handle, uint32_t value);
    esp_err_t (*read_output_reg)(esp_io_expander_handle_t handle, uint32_t *value);
    esp_err_t (*write_direction_reg)(esp_io_expander_handle_t handle, uint32_t value);
    esp_err_t (*read_direction_reg)(esp_io_expander_handle_t handle, uint32_t *value);
    esp_err_t (*reset)(esp_io_expander_handle_t handle);
    esp_err_t (*del)(esp_io_expander_handle_t handle);
    esp_io_expander_config_t config;
};
