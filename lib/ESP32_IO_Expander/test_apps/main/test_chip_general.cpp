/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <memory>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "unity.h"
#include "unity_test_runner.h"
#include "esp_io_expander.hpp"
#include "../../internal/esp_io_expander_private.h"
#include "../../internal/esp_io_expander_lock.h"

using namespace esp_expander;

static const char *TAG = "general_test";

typedef struct {
    esp_io_expander_t base;
    uint32_t direction;
    uint32_t output;
    int fail_next_read;
    int fail_next_write;
    int block_next_read;
    SemaphoreHandle_t read_entered;
    SemaphoreHandle_t read_release;
    int *delete_calls;
} fake_expander_t;

static esp_err_t fake_read_direction(esp_io_expander_handle_t handle, uint32_t *value)
{
    *value = ((fake_expander_t *)handle)->direction;
    vTaskDelay(1);
    return ESP_OK;
}

static esp_err_t fake_write_direction(esp_io_expander_handle_t handle, uint32_t value)
{
    ((fake_expander_t *)handle)->direction = value;
    vTaskDelay(1);
    return ESP_OK;
}

static esp_err_t fake_read_output(esp_io_expander_handle_t handle, uint32_t *value)
{
    fake_expander_t *fake = (fake_expander_t *)handle;
    if (fake->block_next_read) {
        fake->block_next_read = 0;
        xSemaphoreGive(fake->read_entered);
        xSemaphoreTake(fake->read_release, pdMS_TO_TICKS(1000));
    }
    if (fake->fail_next_read) {
        fake->fail_next_read = 0;
        return ESP_ERR_TIMEOUT;
    }
    *value = fake->output;
    vTaskDelay(1);
    return ESP_OK;
}

static esp_err_t fake_write_output(esp_io_expander_handle_t handle, uint32_t value)
{
    fake_expander_t *fake = (fake_expander_t *)handle;
    if (fake->fail_next_write) {
        fake->fail_next_write = 0;
        return ESP_ERR_TIMEOUT;
    }
    fake->output = value;
    vTaskDelay(1);
    return ESP_OK;
}

static esp_err_t fake_delete(esp_io_expander_handle_t handle)
{
    fake_expander_t *fake = (fake_expander_t *)handle;
    if (fake->delete_calls != NULL) ++(*fake->delete_calls);
    free(fake);
    return ESP_OK;
}

static void fake_expander_init(fake_expander_t *fake)
{
    memset(fake, 0, sizeof(*fake));
    fake->base.config.io_count = 8;
    fake->base.read_direction_reg = fake_read_direction;
    fake->base.write_direction_reg = fake_write_direction;
    fake->base.read_output_reg = fake_read_output;
    fake->base.write_output_reg = fake_write_output;
    fake->base.operation_mutex = xSemaphoreCreateRecursiveMutex();
    fake->base.lifecycle_idle = xSemaphoreCreateBinary();
    portMUX_INITIALIZE(&fake->base.lifecycle_lock);
    fake->base.lifecycle_managed = true;
    fake->base.lifecycle_state = ESP_IO_EXPANDER_LIFECYCLE_ACTIVE;
    fake->base.del = fake_delete;
}

typedef struct {
    esp_io_expander_handle_t handle;
    SemaphoreHandle_t start;
    SemaphoreHandle_t done;
    uint32_t pin;
    esp_err_t result;
} expander_task_args_t;

static void concurrent_pin_writer(void *context)
{
    expander_task_args_t *args = (expander_task_args_t *)context;
    xSemaphoreTake(args->start, portMAX_DELAY);
    args->result = ESP_OK;
    for (int i = 0; i < 40; ++i) {
        if (esp_io_expander_set_level(args->handle, args->pin, 0) != ESP_OK ||
            esp_io_expander_set_level(args->handle, args->pin, 1) != ESP_OK) {
            args->result = ESP_FAIL;
            break;
        }
    }
    args->result = (args->result == ESP_OK)
        ? esp_io_expander_set_level(args->handle, args->pin, 1) : args->result;
    xSemaphoreGive(args->done);
    vTaskDelete(NULL);
}

static void expander_timeout_writer(void *context)
{
    expander_task_args_t *args = (expander_task_args_t *)context;
    xSemaphoreTake(args->start, portMAX_DELAY);
    args->result = esp_io_expander_set_level(args->handle, args->pin, 1);
    xSemaphoreGive(args->done);
    vTaskDelete(NULL);
}

TEST_CASE("expander composite writes preserve neighboring output bits under concurrency", "[io_expander][mutex]")
{
    fake_expander_t fake;
    fake_expander_init(&fake);
    TEST_ASSERT_NOT_NULL(fake.base.operation_mutex);
    fake.output = BIT(4) | BIT(5);
    TEST_ASSERT_EQUAL(ESP_OK, esp_io_expander_set_dir(&fake.base, 0xFF, IO_EXPANDER_OUTPUT));

    SemaphoreHandle_t start = xSemaphoreCreateCounting(2, 0);
    SemaphoreHandle_t done = xSemaphoreCreateCounting(2, 0);
    TEST_ASSERT_NOT_NULL(start);
    TEST_ASSERT_NOT_NULL(done);
    expander_task_args_t first = { &fake.base, start, done, BIT(0), ESP_FAIL };
    expander_task_args_t second = { &fake.base, start, done, BIT(1), ESP_FAIL };
    TEST_ASSERT_EQUAL(pdPASS, xTaskCreate(concurrent_pin_writer, "exp_io0", 3072, &first, 2, NULL));
    TEST_ASSERT_EQUAL(pdPASS, xTaskCreate(concurrent_pin_writer, "exp_io1", 3072, &second, 2, NULL));
    xSemaphoreGive(start);
    xSemaphoreGive(start);
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(done, pdMS_TO_TICKS(3000)));
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(done, pdMS_TO_TICKS(3000)));
    TEST_ASSERT_EQUAL(ESP_OK, first.result);
    TEST_ASSERT_EQUAL(ESP_OK, second.result);
    TEST_ASSERT_EQUAL(BIT(0) | BIT(1) | BIT(4) | BIT(5), fake.output);

    vSemaphoreDelete(start);
    vSemaphoreDelete(done);
    vSemaphoreDelete(fake.base.operation_mutex);
    vSemaphoreDelete(fake.base.lifecycle_idle);
}

TEST_CASE("expander mutex timeout is finite and does not poison later calls", "[io_expander][mutex]")
{
    fake_expander_t fake;
    fake_expander_init(&fake);
    TEST_ASSERT_NOT_NULL(fake.base.operation_mutex);
    TEST_ASSERT_EQUAL(ESP_OK, esp_io_expander_set_dir(&fake.base, 0xFF, IO_EXPANDER_OUTPUT));
    SemaphoreHandle_t start = xSemaphoreCreateBinary();
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(start);
    TEST_ASSERT_NOT_NULL(done);
    expander_task_args_t args = { &fake.base, start, done, BIT(2), ESP_OK };

    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTakeRecursive(fake.base.operation_mutex, 0));
    TEST_ASSERT_EQUAL(pdPASS, xTaskCreate(expander_timeout_writer, "exp_timeout", 3072, &args, 2, NULL));
    xSemaphoreGive(start);
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(done, pdMS_TO_TICKS(500)));
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, args.result);
    xSemaphoreGiveRecursive(fake.base.operation_mutex);
    TEST_ASSERT_EQUAL(ESP_OK, esp_io_expander_set_level(&fake.base, BIT(2), 1));

    vSemaphoreDelete(start);
    vSemaphoreDelete(done);
    vSemaphoreDelete(fake.base.operation_mutex);
    vSemaphoreDelete(fake.base.lifecycle_idle);
}

TEST_CASE("expander releases mutex after read and write errors", "[io_expander][mutex]")
{
    fake_expander_t fake;
    fake_expander_init(&fake);
    TEST_ASSERT_NOT_NULL(fake.base.operation_mutex);
    TEST_ASSERT_EQUAL(ESP_OK, esp_io_expander_set_dir(&fake.base, 0xFF, IO_EXPANDER_OUTPUT));

    fake.fail_next_read = 1;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, esp_io_expander_set_level(&fake.base, BIT(0), 1));
    TEST_ASSERT_EQUAL(ESP_OK, esp_io_expander_set_level(&fake.base, BIT(0), 1));

    fake.fail_next_write = 1;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, esp_io_expander_set_level(&fake.base, BIT(1), 1));
    TEST_ASSERT_EQUAL(ESP_OK, esp_io_expander_set_level(&fake.base, BIT(1), 1));
    TEST_ASSERT_EQUAL(BIT(0) | BIT(1), fake.output);

    vSemaphoreDelete(fake.base.operation_mutex);
    vSemaphoreDelete(fake.base.lifecycle_idle);
}

static void expander_delete_task(void *context)
{
    expander_task_args_t *args = (expander_task_args_t *)context;
    xSemaphoreTake(args->start, portMAX_DELAY);
    args->result = esp_io_expander_del(args->handle);
    xSemaphoreGive(args->done);
    vTaskDelete(NULL);
}

static void wait_for_admitted_operations(fake_expander_t *fake, uint32_t count)
{
    TickType_t start = xTaskGetTickCount();
    while ((TickType_t)(xTaskGetTickCount() - start) < pdMS_TO_TICKS(1000)) {
        portENTER_CRITICAL(&fake->base.lifecycle_lock);
        uint32_t admitted = fake->base.admitted_operations;
        portEXIT_CRITICAL(&fake->base.lifecycle_lock);
        if (admitted == count) return;
        vTaskDelay(1);
    }
    TEST_FAIL_MESSAGE("Timed out waiting for admitted-operation count");
}

static void wait_for_closing(fake_expander_t *fake)
{
    TickType_t start = xTaskGetTickCount();
    while ((TickType_t)(xTaskGetTickCount() - start) < pdMS_TO_TICKS(1000)) {
        portENTER_CRITICAL(&fake->base.lifecycle_lock);
        bool closing = fake->base.lifecycle_state == ESP_IO_EXPANDER_LIFECYCLE_CLOSING;
        portEXIT_CRITICAL(&fake->base.lifecycle_lock);
        if (closing) return;
        vTaskDelay(1);
    }
    TEST_FAIL_MESSAGE("Timed out waiting for lifecycle CLOSING state");
}

TEST_CASE("expander close drains active and mutex-waiting operations and rejects new work", "[io_expander][lifecycle]")
{
    int delete_calls = 0;
    fake_expander_t *fake = (fake_expander_t *)calloc(1, sizeof(*fake));
    TEST_ASSERT_NOT_NULL(fake);
    fake_expander_init(fake);
    fake->delete_calls = &delete_calls;
    TEST_ASSERT_EQUAL(ESP_OK, esp_io_expander_set_dir(&fake->base, 0xFF, IO_EXPANDER_OUTPUT));
    fake->read_entered = xSemaphoreCreateBinary();
    fake->read_release = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(fake->read_entered);
    TEST_ASSERT_NOT_NULL(fake->read_release);
    SemaphoreHandle_t read_entered = fake->read_entered;
    SemaphoreHandle_t read_release = fake->read_release;
    fake->block_next_read = 1;

    SemaphoreHandle_t start = xSemaphoreCreateCounting(3, 0);
    SemaphoreHandle_t done = xSemaphoreCreateCounting(3, 0);
    TEST_ASSERT_NOT_NULL(start);
    TEST_ASSERT_NOT_NULL(done);
    expander_task_args_t first = { &fake->base, start, done, BIT(0), ESP_FAIL };
    expander_task_args_t second = { &fake->base, start, done, BIT(1), ESP_FAIL };
    expander_task_args_t closer = { &fake->base, start, done, 0, ESP_FAIL };
    TEST_ASSERT_EQUAL(pdPASS, xTaskCreate(expander_timeout_writer, "life_op0", 3072, &first, 2, NULL));
    TEST_ASSERT_EQUAL(pdPASS, xTaskCreate(expander_timeout_writer, "life_op1", 3072, &second, 2, NULL));
    TEST_ASSERT_EQUAL(pdPASS, xTaskCreate(expander_delete_task, "life_del", 3072, &closer, 2, NULL));

    xSemaphoreGive(start);
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(fake->read_entered, pdMS_TO_TICKS(500)));
    xSemaphoreGive(start);
    wait_for_admitted_operations(fake, 2);
    xSemaphoreGive(start);
    wait_for_closing(fake);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      esp_io_expander_set_level(&fake->base, BIT(2), 1));

    xSemaphoreGive(fake->read_release);
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(done, pdMS_TO_TICKS(1000)));
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(done, pdMS_TO_TICKS(1000)));
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(done, pdMS_TO_TICKS(1000)));
    TEST_ASSERT_EQUAL(ESP_OK, first.result);
    TEST_ASSERT_EQUAL(ESP_OK, second.result);
    TEST_ASSERT_EQUAL(ESP_OK, closer.result);
    TEST_ASSERT_EQUAL(1, delete_calls);

    vSemaphoreDelete(start);
    vSemaphoreDelete(done);
    vSemaphoreDelete(read_entered);
    vSemaphoreDelete(read_release);
}

TEST_CASE("expander close timeout cancels without destroying resources", "[io_expander][lifecycle]")
{
    int delete_calls = 0;
    fake_expander_t *fake = (fake_expander_t *)calloc(1, sizeof(*fake));
    TEST_ASSERT_NOT_NULL(fake);
    fake_expander_init(fake);
    fake->delete_calls = &delete_calls;
    TEST_ASSERT_EQUAL(ESP_OK, esp_io_expander_set_dir(&fake->base, 0xFF, IO_EXPANDER_OUTPUT));
    fake->read_entered = xSemaphoreCreateBinary();
    fake->read_release = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(fake->read_entered);
    TEST_ASSERT_NOT_NULL(fake->read_release);
    SemaphoreHandle_t read_entered = fake->read_entered;
    SemaphoreHandle_t read_release = fake->read_release;
    fake->block_next_read = 1;
    SemaphoreHandle_t start = xSemaphoreCreateBinary();
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(start);
    TEST_ASSERT_NOT_NULL(done);
    expander_task_args_t waiter = { &fake->base, start, done, BIT(2), ESP_FAIL };

    TEST_ASSERT_EQUAL(pdPASS, xTaskCreate(expander_timeout_writer, "life_wait", 3072, &waiter, 2, NULL));
    xSemaphoreGive(start);
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(fake->read_entered, pdMS_TO_TICKS(500)));

    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, esp_io_expander_del(&fake->base));
    TEST_ASSERT_EQUAL(0, delete_calls);
    TEST_ASSERT_EQUAL(ESP_IO_EXPANDER_LIFECYCLE_ACTIVE, fake->base.lifecycle_state);
    TEST_ASSERT_NOT_NULL(fake->base.operation_mutex);

    xSemaphoreGive(fake->read_release);
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(done, pdMS_TO_TICKS(500)));
    TEST_ASSERT_EQUAL(ESP_OK, waiter.result);
    TEST_ASSERT_EQUAL(ESP_OK, esp_io_expander_set_level(&fake->base, BIT(3), 1));
    TEST_ASSERT_EQUAL(ESP_OK, esp_io_expander_del(&fake->base));
    TEST_ASSERT_EQUAL(1, delete_calls);

    vSemaphoreDelete(start);
    vSemaphoreDelete(done);
    vSemaphoreDelete(read_entered);
    vSemaphoreDelete(read_release);
}

/* The following default configurations are for the board 'Espressif: ESP32_S3_LCD_EV_BOARD_V1_5, TCA9554' */
#define TEST_HOST_ID            (I2C_NUM_0)
#define TEST_HOST_I2C_SCL_PIN   (48)
#define TEST_HOST_I2C_SDA_PIN   (47)
#define TEST_DEVICE_ADDRESS     (ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000)

#define HOST_CONFIG_DEFAULT(scl, sda) \
    { \
        .mode = I2C_MODE_MASTER, \
        .sda_io_num = (sda), \
        .scl_io_num = (scl), \
        .sda_pullup_en = GPIO_PULLUP_ENABLE, \
        .scl_pullup_en = GPIO_PULLUP_ENABLE, \
        .master = { \
            .clk_speed = 400000, \
        }, \
        .clk_flags = I2C_SCLK_SRC_FLAG_FOR_NOMAL, \
    }

#define _CREATE_DEVICE(name, ...) \
    ({ \
        ESP_LOGI(TAG, "Create device: " #name); \
        std::shared_ptr<Base> device = std::make_shared<name>(__VA_ARGS__); \
        TEST_ASSERT_NOT_NULL_MESSAGE(device, "Create device failed"); \
        device; \
    })
#define CREATE_DEVICE(name, ...) _CREATE_DEVICE(name, ##__VA_ARGS__)

static void init_host(void)
{
    const i2c_config_t i2c_config = HOST_CONFIG_DEFAULT(TEST_HOST_I2C_SCL_PIN, TEST_HOST_I2C_SDA_PIN);
    TEST_ASSERT_EQUAL(i2c_param_config(TEST_HOST_ID, &i2c_config), ESP_OK);
    TEST_ASSERT_EQUAL(i2c_driver_install(TEST_HOST_ID, i2c_config.mode, 0, 0, 0), ESP_OK);
}

static void deinit_host(void)
{
    TEST_ASSERT_EQUAL(i2c_driver_delete(TEST_HOST_ID), ESP_OK);
}

static void test_device(std::shared_ptr<Base> device)
{
    TEST_ASSERT_MESSAGE(device->init(), "Device initialization failed");
    TEST_ASSERT_MESSAGE(device->begin(), "Device begin failed");
    TEST_ASSERT_MESSAGE(device->reset(), "Device reset failed");
    TEST_ASSERT_MESSAGE(device->del(), "Device del failed");
}

#define CREATE_TEST_CASE(device_name) \
    TEST_CASE("test " #device_name " general functions", "[io_expander][general][" #device_name "]") \
    { \
        std::shared_ptr<Base> expander = nullptr; \
        \
        ESP_LOGI(TAG, "Initialize I2C host"); \
        init_host(); \
        \
        ESP_LOGI(TAG, "Test constructor with (int host_id, uint8_t address) (external I2C)"); \
        expander = CREATE_DEVICE(device_name, TEST_HOST_ID, TEST_DEVICE_ADDRESS); \
        test_device(expander); \
        expander = nullptr; \
        \
        ESP_LOGI(TAG, "Test constructor with (const Config &config) (external I2C)"); \
        Base::Config external_i2c_config = { \
            .host_id = TEST_HOST_ID, \
            .device = Base::DeviceConfig{ \
                .address = TEST_DEVICE_ADDRESS, \
            }, \
        }; \
        expander = CREATE_DEVICE(device_name, external_i2c_config); \
        test_device(expander); \
        expander = nullptr; \
        \
        ESP_LOGI(TAG, "Deinitialize I2C host"); \
        deinit_host(); \
        \
        ESP_LOGI(TAG, "Test constructor with (int scl_io, int sda_io, uint8_t address) (internal I2C)"); \
        expander = CREATE_DEVICE(device_name, TEST_HOST_I2C_SCL_PIN, TEST_HOST_I2C_SDA_PIN, TEST_DEVICE_ADDRESS); \
        test_device(expander); \
        expander = nullptr; \
        \
        ESP_LOGI(TAG, "Test constructor with (const Config &config) (internal I2C)"); \
        Base::Config internal_i2c_config = { \
            .host_id = TEST_HOST_ID, \
            .host = Base::HostPartialConfig{ \
                .sda_io_num = TEST_HOST_I2C_SDA_PIN, \
                .scl_io_num = TEST_HOST_I2C_SCL_PIN, \
            }, \
            .device = Base::DeviceConfig{ \
                .address = TEST_DEVICE_ADDRESS, \
            }, \
        }; \
        expander = CREATE_DEVICE(device_name, internal_i2c_config); \
        test_device(expander); \
        expander = nullptr; \
        \
        expander = CREATE_DEVICE(device_name, TEST_HOST_I2C_SCL_PIN, TEST_HOST_I2C_SDA_PIN, TEST_DEVICE_ADDRESS); \
        TEST_ASSERT_MESSAGE(expander->init(), "Device initialization failed"); \
        TEST_ASSERT_MESSAGE(expander->begin(), "Device begin failed"); \
        \
        ESP_LOGI(TAG, "Test input/output functions"); \
        ESP_LOGI(TAG, "Original status:"); \
        TEST_ASSERT_MESSAGE(expander->printStatus(), "Print status failed"); \
        \
        TEST_ASSERT_MESSAGE(expander->pinMode(0, OUTPUT), "Set pin 0 to output mode failed"); \
        TEST_ASSERT_MESSAGE(expander->pinMode(1, OUTPUT), "Set pin 1 to output mode failed"); \
        TEST_ASSERT_MESSAGE( \
            expander->multiPinMode(IO_EXPANDER_PIN_NUM_2 | IO_EXPANDER_PIN_NUM_3, OUTPUT), \
            "Set pin 2-3 to output mode failed" \
        ); \
        \
        ESP_LOGI(TAG, "Set pint 0-3 to output mode:"); \
        TEST_ASSERT_MESSAGE(expander->printStatus(), "Print status failed"); \
        \
        TEST_ASSERT_MESSAGE(expander->digitalWrite(0, LOW), "Set pin 0 to low level failed"); \
        TEST_ASSERT_MESSAGE(expander->digitalWrite(1, LOW), "Set pin 1 to low level failed"); \
        TEST_ASSERT_MESSAGE( \
            expander->multiDigitalWrite(IO_EXPANDER_PIN_NUM_2 | IO_EXPANDER_PIN_NUM_3, LOW), \
            "Set pin 2-3 to low level failed" \
        ); \
        \
        ESP_LOGI(TAG, "Set pint 0-3 to low level:"); \
        TEST_ASSERT_MESSAGE(expander->printStatus(), "Print status failed"); \
        \
        TEST_ASSERT_MESSAGE(expander->pinMode(0, INPUT), "Set pin 0 to input mode failed"); \
        TEST_ASSERT_MESSAGE(expander->pinMode(1, INPUT), "Set pin 1 to input mode failed"); \
        TEST_ASSERT_MESSAGE( \
            expander->multiPinMode(IO_EXPANDER_PIN_NUM_2 | IO_EXPANDER_PIN_NUM_3, INPUT), \
            "Set pin 2-3 to input mode failed" \
        ); \
        \
        ESP_LOGI(TAG, "Set pint 0-3 to input mode:"); \
        TEST_ASSERT_MESSAGE(expander->printStatus(), "Print status failed"); \
        \
        int level[4] = {0, 0, 0, 0}; \
        int64_t level_temp; \
        \
        level[0] = expander->digitalRead(0); \
        TEST_ASSERT_MESSAGE(level[0] >= 0, "Read pin 0 level failed"); \
        level[1] = expander->digitalRead(1); \
        TEST_ASSERT_MESSAGE(level[1] >= 0, "Read pin 0 level failed"); \
        level_temp = expander->multiDigitalRead(IO_EXPANDER_PIN_NUM_2 | IO_EXPANDER_PIN_NUM_3); \
        TEST_ASSERT_MESSAGE(level_temp >= 0, "Read pin 2-3 level failed"); \
        level[2] = level_temp & IO_EXPANDER_PIN_NUM_2 ? HIGH : LOW; \
        level[3] = level_temp & IO_EXPANDER_PIN_NUM_3 ? HIGH : LOW; \
        ESP_LOGI(TAG, "Pin 0-3 level: %d %d %d %d", level[0], level[1], level[2], level[3]); \
    }

/**
 * Here to create test cases for different devices
 */
CREATE_TEST_CASE(TCA95XX_8BIT)
CREATE_TEST_CASE(TCA95XX_16BIT)
CREATE_TEST_CASE(CH422G)
CREATE_TEST_CASE(HT8574)
