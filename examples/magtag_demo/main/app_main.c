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
#include "nvs_flash.h"
#include "wifi.h"
#include "ws2812_led_strip.h"
#include "golioth.h"

#define TAG "magtag_demo"

// TODO - GPIO buttons
// TODO - epaper display
// TODO - lis3dh (i2c) accelerometer
// TODO - DAC speaker/buzzer + GPIO enable
// (https://github.com/espressif/esp-idf/tree/master/examples/peripherals/rmt/musical_buzzer)
// TODO - ADC light sensor

#define D13_LED_GPIO_PIN 13

static void on_client_event(golioth_client_t client, golioth_client_event_t event, void* arg) {
    bool connected = (event == GOLIOTH_CLIENT_EVENT_CONNECTED);
    ESP_LOGI(TAG, "Golioth client %s", connected ? "connected" : "disconnected");
    led_strip_rgb_t rgb = (connected ? GREEN : BLUE);
    ws2812_led_strip_set_all_rgb_immediate(rgb);
}

static void app_gpio_init(void) {
    gpio_install_isr_service(0);

    // D13 LED
    gpio_reset_pin(D13_LED_GPIO_PIN);
    gpio_set_direction(D13_LED_GPIO_PIN, GPIO_MODE_OUTPUT);
}

void app_main(void) {
    nvs_flash_init();
    app_gpio_init();

    bool d13_on = true;
    gpio_set_level(D13_LED_GPIO_PIN, d13_on);

    ws2812_led_strip_init();
    ws2812_led_strip_set_all_rgb_immediate(YELLOW);

    wifi_init(CONFIG_GOLIOTH_EXAMPLE_WIFI_SSID, CONFIG_GOLIOTH_EXAMPLE_WIFI_PSK);
    wifi_wait_for_connected();

    ws2812_led_strip_set_all_rgb_immediate(BLUE);

    golioth_client_t client = golioth_client_create(
            CONFIG_GOLIOTH_EXAMPLE_COAP_PSK_ID, CONFIG_GOLIOTH_EXAMPLE_COAP_PSK);
    assert(client);
    golioth_client_register_event_callback(client, on_client_event, NULL);

    while (1) {
        d13_on = !d13_on;
        gpio_set_level(D13_LED_GPIO_PIN, d13_on);
        vTaskDelay(250 / portTICK_PERIOD_MS);
    };
}
