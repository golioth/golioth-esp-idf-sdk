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
#include "buttons.h"
#include "board.h"
#include "events.h"
#include "golioth.h"

#define TAG "magtag_demo"

// TODO - epaper display
// TODO - lis3dh (i2c) accelerometer
// TODO - DAC speaker/buzzer + GPIO enable
// (https://github.com/espressif/esp-idf/tree/master/examples/peripherals/rmt/musical_buzzer)
// TODO - ADC light sensor

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

    bool d13_on = true;
    gpio_set_level(D13_LED_GPIO_PIN, d13_on);

    leds_init();
    leds_set_all_immediate(YELLOW);

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
        if (events & EVENT_BUTTON_ANY) {
            buttons_handle_event(events & EVENT_BUTTON_ANY);
        }
        if (events & EVENT_TIMER_250MS) {
            d13_on = !d13_on;
            gpio_set_level(D13_LED_GPIO_PIN, d13_on);
        }
    };
}
