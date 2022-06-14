/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include <cJSON.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "wifi.h"
#include "leds.h"
#include "i2c.h"
#include "buttons.h"
#include "light_sensor.h"
#include "epaper.h"
#include "lis3dh.h"
#include "speaker.h"
#include "audio/coin.h"
#include "board.h"
#include "events.h"
#include "golioth.h"

#define TAG "magtag_demo"

// TODO - Golioth integration

static TimerHandle_t _timer250ms;
EventGroupHandle_t _event_group;

static void on_timer250ms(TimerHandle_t xTimer) {
    xEventGroupSetBits(_event_group, EVENT_TIMER_250MS);
}

static void on_client_event(golioth_client_t client, golioth_client_event_t event, void* arg) {
    bool connected = (event == GOLIOTH_CLIENT_EVENT_CONNECTED);
    ESP_LOGI(TAG, "Golioth client %s", connected ? "connected" : "disconnected");
    uint32_t rgb = (connected ? GREEN : BLUE);
    leds_set_all_immediate(rgb);
    epaper_autowrite((uint8_t*)"Connected to Golioth!");
}

static void app_gpio_init(void) {
    gpio_install_isr_service(0);
    gpio_reset_pin(D13_LED_GPIO_PIN);
    gpio_set_direction(D13_LED_GPIO_PIN, GPIO_MODE_OUTPUT);
    buttons_gpio_init();
}

void app_main(void) {
    _event_group = xEventGroupCreate();
    _timer250ms = xTimerCreate("timer250ms", 250 / portTICK_PERIOD_MS, pdTRUE, NULL, on_timer250ms);
    xTimerStart(_timer250ms, 0);

    nvs_flash_init();
    app_gpio_init();
    i2c_master_init(I2C_SCL_PIN, I2C_SDA_PIN);
    lis3dh_init(LIS3DH_I2C_ADDR);
    speaker_init(SPEAKER_DAC1_PIN, SPEAKER_ENABLE_PIN);
    light_sensor_init();

    bool d13_on = true;
    gpio_set_level(D13_LED_GPIO_PIN, d13_on);

    leds_init();
    leds_set_all_immediate(YELLOW);

    epaper_init();

    wifi_init(CONFIG_GOLIOTH_EXAMPLE_WIFI_SSID, CONFIG_GOLIOTH_EXAMPLE_WIFI_PSK);
    wifi_wait_for_connected();

    leds_set_all_immediate(BLUE);

    golioth_client_t client = golioth_client_create(
            CONFIG_GOLIOTH_EXAMPLE_COAP_PSK_ID, CONFIG_GOLIOTH_EXAMPLE_COAP_PSK);
    assert(client);
    golioth_client_register_event_callback(client, on_client_event, NULL);

    while (1) {
        EventBits_t events =
                xEventGroupWaitBits(_event_group, EVENT_ANY, pdTRUE, pdFALSE, portMAX_DELAY);
        if (events & EVENT_BUTTON_A_PRESSED) {
            ESP_LOGI(TAG, "Button A pressed");
        }
        if (events & EVENT_BUTTON_B_PRESSED) {
            ESP_LOGI(TAG, "Button B pressed");
        }
        if (events & EVENT_BUTTON_C_PRESSED) {
            ESP_LOGI(TAG, "Button C pressed");
        }
        if (events & EVENT_BUTTON_D_PRESSED) {
            ESP_LOGI(TAG, "Button D pressed");
            speaker_play_audio(coin_audio, sizeof(coin_audio), COIN_SAMPLE_RATE);
        }
        if (events & EVENT_TIMER_250MS) {
            static int iteration = 0;

            d13_on = !d13_on;
            gpio_set_level(D13_LED_GPIO_PIN, d13_on);

            if ((iteration % 4) == 0) {  // 1 s
                lis3dh_accel_data_t accel_data = {};
                lis3dh_accel_read(&accel_data);
                ESP_LOGI(
                        TAG,
                        "accel (x, y, z) = (%.03f, %.03f, %.03f)",
                        accel_data.x_g,
                        accel_data.y_g,
                        accel_data.z_g);
                ESP_LOGI(TAG, "light sensor = %d mV", light_sensor_read_mV());
            }

            iteration++;
        }
    };
}
