/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include <cJSON.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "nvs.h"
#include "shell.h"
#include "wifi.h"
#include "fw_update.h"
#include "golioth.h"

#define TAG "golioth_example"
static const char* _current_version = "1.2.3";

#define BLINK_GPIO 5
static bool led_state = false;

static void reset_task(void* arg) {
    ESP_LOGI(TAG, "Scheduling restart");
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    esp_restart();
}

static uint8_t on_reset(golioth_client_t client, const char* method, const cJSON* params, uint8_t *detail, size_t detail_size) {
    ESP_LOGI( TAG, "Calling reset on device");
    char *string = cJSON_Print(params);
    ESP_LOGI(
        TAG,
        "Golioth rpc params: %s", string);  
    free(string);  
    bool reset_task_created = xTaskCreate(
                reset_task,
                "reset_task",
                4096,
                NULL,  // task arg
                3,     // pri
                NULL);
    return reset_task_created ? RPC_OK : RPC_RESOURCE_EXHAUSTED;
}

static uint8_t on_toggle(golioth_client_t client, const char* method, const cJSON* params, uint8_t *detail, size_t detail_size) {
    ESP_LOGI( TAG, "Calling toggle on device");
    char *string = cJSON_Print(params);
    ESP_LOGI(
        TAG,
        "Golioth rpc params: %s", string);  
    free(string);  
    led_state = !led_state;
    gpio_set_level(BLINK_GPIO, led_state ? 1 : 0);
    return RPC_OK;
}

static uint8_t on_random(golioth_client_t client, const char* method, const cJSON* params, uint8_t *detail, size_t detail_size) {
    ESP_LOGI( TAG, "Calling random on device");
    char *string = cJSON_Print(params);
    ESP_LOGI(
        TAG,
        "Golioth rpc params: %s", string);  
    free(string);  

    snprintf((const char*)detail, detail_size, "{ \"value\": %d }", rand());
    return RPC_OK;
}

static void on_client_event(golioth_client_t client, golioth_client_event_t event, void* arg) {
    ESP_LOGI(
            TAG,
            "Golioth client %s",
            event == GOLIOTH_CLIENT_EVENT_CONNECTED ? "connected" : "disconnected");
}

void app_main(void) {
    nvs_init();
    shell_start();

    gpio_pad_select_gpio(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(BLINK_GPIO, 0);

    if (!nvs_credentials_are_set()) {
        while (1) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            ESP_LOGW(TAG, "WiFi and golioth credentials are not set");
            ESP_LOGW(
                    TAG,
                    "Use the shell commands wifi_set and golioth_set to set them, then restart");
            vTaskDelay(portMAX_DELAY);
        }
    }

    wifi_init(nvs_read_wifi_ssid(), nvs_read_wifi_password());
    wifi_wait_for_connected();

    golioth_client_t client =
            golioth_client_create(nvs_read_golioth_psk_id(), nvs_read_golioth_psk());
    assert(client);

    // Spawn background task that will perform firmware updates (aka OTA update)
    fw_update_init(client, _current_version);

    golioth_client_register_event_callback(client, on_client_event, NULL);
    
    // Listen to RPC calls
    golioth_rpc_register(client, "reset", on_reset);
    golioth_rpc_register(client, "toggle", on_toggle);
    golioth_rpc_register(client, "random", on_random);

    while (1) {        
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    };
}
