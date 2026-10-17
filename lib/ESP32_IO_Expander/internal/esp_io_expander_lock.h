/* Private synchronization helpers for ESP_IO_Expander drivers.
 * Lock order: caller service lock -> SPI/card lock (if required) -> brief
 * lifecycle critical section -> CH422G operation mutex -> ESP-IDF I2C mutex.
 * The lifecycle critical section is never held while taking another lock,
 * waiting, or doing I/O. Never acquire SPI, LVGL, or a service lock while the
 * operation mutex is held.
 */
#pragma once

#include "esp_io_expander_private.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_IO_EXPANDER_OPERATION_TIMEOUT_MS (50)
#define ESP_IO_EXPANDER_CLOSE_TIMEOUT_MS     (250)

/* The short lifecycle critical section only changes state/refcounts. It is
 * always released before waiting for operation_mutex or performing I2C.
 */
static inline esp_err_t esp_io_expander_operation_lock(esp_io_expander_handle_t handle)
{
    if (handle == NULL) return ESP_ERR_INVALID_ARG;
    if (handle->operation_mutex == NULL) return ESP_OK;
    return xSemaphoreTakeRecursive(handle->operation_mutex,
                                   pdMS_TO_TICKS(ESP_IO_EXPANDER_OPERATION_TIMEOUT_MS)) == pdTRUE
        ? ESP_OK : ESP_ERR_TIMEOUT;
}

static inline void esp_io_expander_operation_unlock(esp_io_expander_handle_t handle)
{
    if (handle != NULL && handle->operation_mutex != NULL) {
        (void)xSemaphoreGiveRecursive(handle->operation_mutex);
    }
}

/* Admission counts tasks before they wait on the per-device mutex. */
static inline esp_err_t esp_io_expander_operation_begin(esp_io_expander_handle_t handle)
{
    if (handle == NULL) return ESP_ERR_INVALID_ARG;
    if (!handle->lifecycle_managed) return esp_io_expander_operation_lock(handle);

    portENTER_CRITICAL(&handle->lifecycle_lock);
    if (handle->lifecycle_state != ESP_IO_EXPANDER_LIFECYCLE_ACTIVE) {
        portEXIT_CRITICAL(&handle->lifecycle_lock);
        return ESP_ERR_INVALID_STATE;
    }
    ++handle->admitted_operations;
    portEXIT_CRITICAL(&handle->lifecycle_lock);

    esp_err_t ret = esp_io_expander_operation_lock(handle);
    if (ret != ESP_OK) {
        /* No operation mutex was acquired, but this admission still counts. */
        portENTER_CRITICAL(&handle->lifecycle_lock);
        --handle->admitted_operations;
        bool notify_idle = (handle->lifecycle_state == ESP_IO_EXPANDER_LIFECYCLE_CLOSING &&
                            handle->admitted_operations == 0);
        portEXIT_CRITICAL(&handle->lifecycle_lock);
        if (notify_idle) (void)xSemaphoreGive(handle->lifecycle_idle);
    }
    return ret;
}

static inline void esp_io_expander_operation_end(esp_io_expander_handle_t handle)
{
    if (handle == NULL) return;
    esp_io_expander_operation_unlock(handle);
    if (!handle->lifecycle_managed) return;

    portENTER_CRITICAL(&handle->lifecycle_lock);
    --handle->admitted_operations;
    bool notify_idle = (handle->lifecycle_state == ESP_IO_EXPANDER_LIFECYCLE_CLOSING &&
                        handle->admitted_operations == 0);
    portEXIT_CRITICAL(&handle->lifecycle_lock);
    if (notify_idle) (void)xSemaphoreGive(handle->lifecycle_idle);
}

#ifdef __cplusplus
}
#endif
