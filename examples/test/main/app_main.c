/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include <cJSON.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "unity.h"
#include "nvs.h"
#include "wifi.h"
#include "time.h"
#include "shell.h"
#include "fw_update.h"
#include "util.h"
#include "golioth.h"

#define TAG "test"

static const char* _current_version = "1.2.3";

static SemaphoreHandle_t _connected_sem;
static SemaphoreHandle_t _disconnected_sem;
static golioth_client_t _client;
static uint32_t _initial_free_heap;

static void on_client_event(golioth_client_t client, golioth_client_event_t event, void* arg) {
    bool is_connected = (event == GOLIOTH_CLIENT_EVENT_CONNECTED);
    ESP_LOGI(TAG, "Golioth %s", is_connected ? "connected" : "disconnected");
    if (is_connected) {
        xSemaphoreGive(_connected_sem);
    } else {
        xSemaphoreGive(_disconnected_sem);
    }
}

static void test_connects_to_wifi(void) {
    static bool run_once = false;
    if (run_once) {
        return;
    }
    run_once = true;
    wifi_init(nvs_read_wifi_ssid(), nvs_read_wifi_password());
    TEST_ASSERT_TRUE(wifi_wait_for_connected_with_timeout(10));
}

static void test_golioth_client_create(void) {
    if (!_client) {
        _client = golioth_client_create(nvs_read_golioth_psk_id(), nvs_read_golioth_psk());
        TEST_ASSERT_NOT_NULL(_client);
        golioth_client_register_event_callback(_client, on_client_event, NULL);
    }
}

static void test_connects_to_golioth(void) {
    TEST_ASSERT_NOT_NULL(_client);
    TEST_ASSERT_EQUAL(GOLIOTH_OK, golioth_client_start(_client));
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(_connected_sem, 5000 / portTICK_PERIOD_MS));
}

static void test_client_stop_and_start(void) {
    TEST_ASSERT_EQUAL(GOLIOTH_OK, golioth_client_stop(_client));
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(_disconnected_sem, 3000 / portTICK_PERIOD_MS));
    TEST_ASSERT_EQUAL(GOLIOTH_OK, golioth_client_start(_client));
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(_connected_sem, 5000 / portTICK_PERIOD_MS));
}

static void test_wifi_stop_and_start(void) {
    TEST_ASSERT_EQUAL(ESP_OK, esp_wifi_stop());
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    TEST_ASSERT_EQUAL(ESP_OK, esp_wifi_start());
    TEST_ASSERT_TRUE(wifi_wait_for_connected_with_timeout(10));
}

static void test_golioth_client_heap_usage(void) {
    uint32_t post_connect_free_heap = esp_get_free_heap_size();
    int32_t golioth_heap_usage = _initial_free_heap - post_connect_free_heap;
    ESP_LOGI(TAG, "golioth_heap_usage = %u", golioth_heap_usage);
    TEST_ASSERT_TRUE(golioth_heap_usage < 50000);
}

static void test_request_dropped_if_client_not_running(void) {
    TEST_ASSERT_EQUAL(GOLIOTH_OK, golioth_client_stop(_client));
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(_disconnected_sem, 3000 / portTICK_PERIOD_MS));

    // Verify each request type returns proper state
    TEST_ASSERT_EQUAL(GOLIOTH_ERR_INVALID_STATE, golioth_lightdb_set_int_async(_client, "a", 1));
    TEST_ASSERT_EQUAL(
            GOLIOTH_ERR_INVALID_STATE, golioth_lightdb_get_async(_client, "a", NULL, NULL));
    TEST_ASSERT_EQUAL(GOLIOTH_ERR_INVALID_STATE, golioth_lightdb_delete_async(_client, "a"));
    TEST_ASSERT_EQUAL(
            GOLIOTH_ERR_INVALID_STATE, golioth_lightdb_observe_async(_client, "a", NULL, NULL));

    TEST_ASSERT_EQUAL(GOLIOTH_OK, golioth_client_start(_client));
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(_connected_sem, 5000 / portTICK_PERIOD_MS));
}

static void test_request_timeout_if_wifi_disconnects(void) {}

static int built_in_test(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_connects_to_wifi);
    if (!_initial_free_heap) {
        // Snapshot of heap usage after connecting to WiFi. This is baseline/reference
        // which we compare against when gauging how much RAM the Golioth client uses.
        _initial_free_heap = esp_get_free_heap_size();
    }
    RUN_TEST(test_golioth_client_create);
    RUN_TEST(test_connects_to_golioth);
    RUN_TEST(test_golioth_client_heap_usage);
    RUN_TEST(test_client_stop_and_start);
    RUN_TEST(test_wifi_stop_and_start);
    RUN_TEST(test_request_dropped_if_client_not_running);
    RUN_TEST(test_request_timeout_if_wifi_disconnects);
    UNITY_END();

    return 0;
}

static int start_ota(int argc, char** argv) {
    // Ensure we have a connected client (in case this is the first command run)
    if (!_client) {
        test_connects_to_wifi();
        test_golioth_client_create();
        test_connects_to_golioth();
    }
    fw_update_init(_client, _current_version);
    return 0;
}

void app_main(void) {
    nvs_init();

    _connected_sem = xSemaphoreCreateBinary();
    _disconnected_sem = xSemaphoreCreateBinary();

    esp_console_cmd_t built_in_test_cmd = {
            .command = "built_in_test",
            .help = "Run the built-in Unity tests",
            .func = built_in_test,
    };
    shell_register_command(&built_in_test_cmd);

    esp_console_cmd_t start_ota_cmd = {
            .command = "start_ota",
            .help = "Start the firmware update OTA task",
            .func = start_ota,
    };
    shell_register_command(&start_ota_cmd);
    shell_start();

    while (1) {
        delay_ms(100000);
    };
}
