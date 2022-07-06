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
#include "esp_log.h"
#include "unity.h"
#include "nvs.h"
#include "wifi.h"
#include "time.h"
#include "shell.h"
#include "util.h"
#include "golioth.h"

#define TAG "test"

static const char* _current_version = "1.2.3";

static SemaphoreHandle_t _connected_sem;
static SemaphoreHandle_t _disconnected_sem;
static golioth_client_t _client;
static uint32_t _initial_free_heap;
static bool _wifi_connected;

// Note: Don't put TEST_ASSERT_* statements in client callback functions, as this
// will cause a stack overflow in the client task is any of the assertions fail.
//
// I'm not sure exactly why this happens, but I suspect it's related to
// Unity's UNITY_FAIL_AND_BAIL macro that gets called on failing assertions.

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
    if (_wifi_connected) {
        return;
    }
    wifi_init(nvs_read_wifi_ssid(), nvs_read_wifi_password());
    TEST_ASSERT_TRUE(wifi_wait_for_connected_with_timeout(10));
    _wifi_connected = true;
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

static void test_golioth_client_heap_usage(void) {
    uint32_t post_connect_free_heap = esp_get_minimum_free_heap_size();
    int32_t golioth_heap_usage = _initial_free_heap - post_connect_free_heap;
    ESP_LOGI(TAG, "Estimated heap usage by Golioth stack = %u", golioth_heap_usage);
    TEST_ASSERT_TRUE(golioth_heap_usage < 50000);
}

static void test_request_dropped_if_client_not_running(void) {
    TEST_ASSERT_EQUAL(GOLIOTH_OK, golioth_client_stop(_client));
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(_disconnected_sem, 3000 / portTICK_PERIOD_MS));

    // Wait another 2 s for client to be fully stopped
    uint64_t timeout_ms = golioth_time_millis() + 2000;
    while (golioth_time_millis() < timeout_ms) {
        if (!golioth_client_is_running(_client)) {
            break;
        }
        golioth_time_delay_ms(100);
    }

    // Verify each request type returns proper state
    TEST_ASSERT_EQUAL(
            GOLIOTH_ERR_INVALID_STATE, golioth_lightdb_set_int_async(_client, "a", 1, NULL, NULL));
    TEST_ASSERT_EQUAL(
            GOLIOTH_ERR_INVALID_STATE, golioth_lightdb_get_async(_client, "a", NULL, NULL));
    TEST_ASSERT_EQUAL(
            GOLIOTH_ERR_INVALID_STATE, golioth_lightdb_delete_async(_client, "a", NULL, NULL));
    TEST_ASSERT_EQUAL(
            GOLIOTH_ERR_INVALID_STATE, golioth_lightdb_observe_async(_client, "a", NULL, NULL));

    TEST_ASSERT_EQUAL(GOLIOTH_OK, golioth_client_start(_client));
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(_connected_sem, 5000 / portTICK_PERIOD_MS));
}

static void test_lightdb_set_get_sync(void) {
    int randint = rand();
    ESP_LOGD(TAG, "randint = %d", randint);
    TEST_ASSERT_EQUAL(GOLIOTH_OK, golioth_lightdb_set_int_sync(_client, "test_int", randint, 3));

    // Delay for a bit. This is done because the value may not have been written to
    // the database on the back end yet.
    //
    // The server responds before the data is written to the database ("eventually consistent"),
    // so there's a chance that if we try to read immediately, we will get the wrong data,
    // so we need to wait to be sure.
    golioth_time_delay_ms(200);

    int32_t get_randint = 0;
    TEST_ASSERT_EQUAL(
            GOLIOTH_OK, golioth_lightdb_get_int_sync(_client, "test_int", &get_randint, 3));
    TEST_ASSERT_EQUAL(randint, get_randint);
}

static bool _on_get_test_int2_called = false;
static int32_t _test_int2_value = 0;
static void on_get_test_int2(
        golioth_client_t client,
        const golioth_response_t* response,
        const char* path,
        const uint8_t* payload,
        size_t payload_size,
        void* arg) {
    golioth_response_t* arg_response = (golioth_response_t*)arg;
    *arg_response = *response;
    _on_get_test_int2_called = true;
    _test_int2_value = golioth_payload_as_int(payload, payload_size);
}

static bool _on_set_test_int2_called = false;
static void on_set_test_int2(
        golioth_client_t client,
        const golioth_response_t* response,
        const char* path,
        void* arg) {
    golioth_response_t* arg_response = (golioth_response_t*)arg;
    *arg_response = *response;
    _on_set_test_int2_called = true;
}

static void test_lightdb_set_get_async(void) {
    _on_set_test_int2_called = false;
    _on_get_test_int2_called = false;
    golioth_response_t set_async_response = {};
    golioth_response_t get_async_response = {};

    int randint = rand();
    TEST_ASSERT_EQUAL(
            GOLIOTH_OK,
            golioth_lightdb_set_int_async(
                    _client, "test_int2", randint, on_set_test_int2, &set_async_response));

    // Wait for response
    uint64_t timeout_ms = golioth_time_millis() + 3000;
    while (golioth_time_millis() < timeout_ms) {
        if (_on_set_test_int2_called) {
            break;
        }
        golioth_time_delay_ms(100);
    }
    TEST_ASSERT_TRUE(_on_set_test_int2_called);
    TEST_ASSERT_EQUAL(GOLIOTH_OK, set_async_response.status);
    TEST_ASSERT_EQUAL(2, set_async_response.class);  // success
    TEST_ASSERT_EQUAL(4, set_async_response.code);   // changed

    TEST_ASSERT_EQUAL(
            GOLIOTH_OK,
            golioth_lightdb_get_async(_client, "test_int2", on_get_test_int2, &get_async_response));

    timeout_ms = golioth_time_millis() + 3000;
    while (golioth_time_millis() < timeout_ms) {
        if (_on_get_test_int2_called) {
            break;
        }
        golioth_time_delay_ms(100);
    }
    TEST_ASSERT_TRUE(_on_get_test_int2_called);
    TEST_ASSERT_EQUAL(GOLIOTH_OK, get_async_response.status);
    TEST_ASSERT_EQUAL(2, get_async_response.class);  // success
    TEST_ASSERT_EQUAL(5, get_async_response.code);   // content
    TEST_ASSERT_EQUAL(randint, _test_int2_value);
}

static bool _on_test_timeout_called = false;
static void on_test_timeout(
        golioth_client_t client,
        const golioth_response_t* response,
        const char* path,
        const uint8_t* payload,
        size_t payload_size,
        void* arg) {
    golioth_response_t* arg_response = (golioth_response_t*)arg;
    *arg_response = *response;
    _on_test_timeout_called = true;
}

// This test takes about 30 seconds to complete
static void test_request_timeout_if_packets_dropped(void) {
    golioth_client_set_packet_loss_percent(100);

    int32_t dummy = 0;
    TEST_ASSERT_EQUAL(
            GOLIOTH_ERR_TIMEOUT,
            golioth_lightdb_get_int_sync(_client, "expect_timeout", &dummy, 1));
    TEST_ASSERT_EQUAL(
            GOLIOTH_ERR_TIMEOUT, golioth_lightdb_set_int_sync(_client, "expect_timeout", 4, 1));

    // TODO - remove surrounding asserts
    assert(_client);
    TEST_ASSERT_EQUAL(
            GOLIOTH_ERR_TIMEOUT, golioth_lightdb_delete_sync(_client, "expect_timeout", 1));
    assert(_client);

    _on_test_timeout_called = false;
    golioth_response_t async_response = {};
    TEST_ASSERT_EQUAL(
            GOLIOTH_OK,
            golioth_lightdb_get_async(_client, "expect_timeout", on_test_timeout, &async_response));

    // Wait up to 12 s for async response to time out
    uint64_t timeout_ms = golioth_time_millis() + 12000;
    while (golioth_time_millis() < timeout_ms) {
        if (_on_test_timeout_called) {
            break;
        }
        golioth_time_delay_ms(100);
    }

    TEST_ASSERT_TRUE(_on_test_timeout_called);
    TEST_ASSERT_EQUAL(GOLIOTH_ERR_TIMEOUT, async_response.status);

    // If a synchronous request is performed with infinite timeout specified,
    // the request will still timeout after CONFIG_GOLIOTH_COAP_RESPONSE_TIMEOUT_S.
    uint64_t now = golioth_time_millis();
    TEST_ASSERT_EQUAL(
            GOLIOTH_ERR_TIMEOUT,
            golioth_lightdb_delete_sync(_client, "expect_timeout", GOLIOTH_WAIT_FOREVER));
    TEST_ASSERT_TRUE(golioth_time_millis() - now > (1000 * CONFIG_GOLIOTH_COAP_RESPONSE_TIMEOUT_S));

    golioth_client_set_packet_loss_percent(0);

    // Wait for connected
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(_connected_sem, 5000 / portTICK_PERIOD_MS));
}

static void test_lightdb_error_if_path_not_found(void) {
    // Issue a sync GET request to an invalid path.
    // Verify a non-success response is received.

    // The server actually gives us a 2.05 response (success) for non-existant paths.
    // In this case, our SDK detects the payload is empty and returns GOLIOTH_ERR_NULL.
    int32_t dummy = 0;
    TEST_ASSERT_EQUAL(
            GOLIOTH_ERR_NULL, golioth_lightdb_get_int_sync(_client, "not_found", &dummy, 3));
}

static void test_client_task_stack_min_remaining(void) {
    uint32_t stack_unused = golioth_client_task_stack_min_remaining(_client);
    uint32_t stack_used = CONFIG_GOLIOTH_COAP_TASK_STACK_SIZE_BYTES - stack_unused;
    ESP_LOGI(TAG, "Client task stack used = %u, unused = %u", stack_used, stack_unused);

    // Verify at least 25% stack was not used
    TEST_ASSERT_TRUE(stack_unused >= CONFIG_GOLIOTH_COAP_TASK_STACK_SIZE_BYTES / 4);
}

static void test_client_destroy_and_no_memory_leaks(void) {
    TEST_ASSERT_EQUAL(GOLIOTH_OK, golioth_client_stop(_client));
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(_disconnected_sem, 3000 / portTICK_PERIOD_MS));

    // Wait another 2 s for client to be fully stopped
    uint64_t timeout_ms = golioth_time_millis() + 2000;
    while (golioth_time_millis() < timeout_ms) {
        if (!golioth_client_is_running(_client)) {
            break;
        }
        golioth_time_delay_ms(100);
    }

    // Request queue should be empty now
    TEST_ASSERT_EQUAL(0, golioth_client_num_items_in_request_queue(_client));

    golioth_client_destroy(_client);
    _client = NULL;

    // Verify all allocations made by the client have been freed
    TEST_ASSERT_FALSE(golioth_client_has_allocation_leaks());
}

static int built_in_test(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_connects_to_wifi);
    if (!_initial_free_heap) {
        // Snapshot of heap usage after connecting to WiFi. This is baseline/reference
        // which we compare against when gauging how much RAM the Golioth client uses.
        _initial_free_heap = esp_get_minimum_free_heap_size();
    }
    if (!_client) {
        RUN_TEST(test_golioth_client_create);
        RUN_TEST(test_connects_to_golioth);
    }
    RUN_TEST(test_lightdb_set_get_sync);
    RUN_TEST(test_lightdb_set_get_async);
    RUN_TEST(test_golioth_client_heap_usage);
    RUN_TEST(test_request_dropped_if_client_not_running);
    RUN_TEST(test_lightdb_error_if_path_not_found);
    RUN_TEST(test_request_timeout_if_packets_dropped);
    RUN_TEST(test_client_task_stack_min_remaining);
    RUN_TEST(test_client_destroy_and_no_memory_leaks);
    UNITY_END();

    return 0;
}

static int start_ota(int argc, char** argv) {
    test_connects_to_wifi();
    if (!_client) {
        test_golioth_client_create();
        test_connects_to_golioth();
    }
    golioth_fw_update_init(_client, _current_version);
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
        golioth_time_delay_ms(100000);
    };
}
