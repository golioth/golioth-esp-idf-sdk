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
#include "golioth.h"

#define TAG "test"

static void on_client_event(golioth_client_t client, golioth_client_event_t event, void* arg) {
    SemaphoreHandle_t connected_sem = (SemaphoreHandle_t)arg;
    bool is_connected = (event == GOLIOTH_CLIENT_EVENT_CONNECTED);
    if (is_connected) {
        ESP_LOGI(TAG, "Golioth connected");
        if (connected_sem) {
            xSemaphoreGive(connected_sem);
        }
    }
}

static void test_connects_to_wifi(void) {
    wifi_init(CONFIG_GOLIOTH_EXAMPLE_WIFI_SSID, CONFIG_GOLIOTH_EXAMPLE_WIFI_PSK);
    bool connected = wifi_wait_for_connected_with_timeout(5);
    TEST_ASSERT_TRUE(connected);
}


static void try_connect(const char* psk_id, const char* psk, bool should_connect) {
    SemaphoreHandle_t connected_sem = xSemaphoreCreateBinary();
    golioth_client_t client = golioth_client_create(psk_id, psk);
    TEST_ASSERT_NOT_NULL(client);
    golioth_client_register_event_callback(client, on_client_event, connected_sem);
    TEST_ASSERT_EQUAL(should_connect, xSemaphoreTake(connected_sem, 5000 / portTICK_PERIOD_MS));
    golioth_client_destroy(client);
    vSemaphoreDelete(connected_sem);
}

static void test_connects_to_golioth(void) {
    try_connect(CONFIG_GOLIOTH_EXAMPLE_COAP_PSK_ID, CONFIG_GOLIOTH_EXAMPLE_COAP_PSK, true);
}

static void test_does_not_connect_to_golioth_with_invalid_psk(void) {
    try_connect(CONFIG_GOLIOTH_EXAMPLE_COAP_PSK_ID, "invalid_psk", false);
}

void app_main(void) {
    nvs_init();

    printf("test BEGIN\n");
    UNITY_BEGIN();
    RUN_TEST(test_connects_to_wifi);
    RUN_TEST(test_connects_to_golioth);
    RUN_TEST(test_does_not_connect_to_golioth_with_invalid_psk);
    UNITY_END();
    printf("test END\n");

    while (1) {
        delay_ms(100000);
    };
}
