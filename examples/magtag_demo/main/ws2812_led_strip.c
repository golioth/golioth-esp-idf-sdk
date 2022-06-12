#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/rmt.h"
#include "led_strip.h"
#include "ws2812_led_strip.h"

#define TAG "ws2812_led_strip"

#define RMT_TX_CHANNEL RMT_CHANNEL_0

// Refer to Adafruit schematic:
//
// https://cdn-learn.adafruit.com/assets/assets/000/096/946/original/adafruit_products_MagTag_sch.png?1605026160
#define LED_STRIP_DATA_PIN 1
#define LED_STRIP_POWER_PIN 21

#define EXAMPLE_CHASE_SPEED_MS (250)

const led_strip_rgb_t BLACK = {.r = 0x00, .g = 0x00, .b = 0x00};
const led_strip_rgb_t RED = {.r = 0xFF, .g = 0x00, .b = 0x00};
const led_strip_rgb_t GREEN = {.r = 0x00, .g = 0xFF, .b = 0x00};
const led_strip_rgb_t BLUE = {.r = 0x00, .g = 0x00, .b = 0xFF};
const led_strip_rgb_t YELLOW = {.r = 0xFF, .g = 0xFF, .b = 0x00};

static led_strip_t* _strip;

void ws2812_led_strip_init(void) {
    // Enable power
    gpio_reset_pin(LED_STRIP_POWER_PIN);
    gpio_set_direction(LED_STRIP_POWER_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_STRIP_POWER_PIN, 0);  // active low

    rmt_config_t config = RMT_DEFAULT_CONFIG_TX(LED_STRIP_DATA_PIN, RMT_TX_CHANNEL);
    // set counter clock to 40MHz
    config.clk_div = 2;

    ESP_ERROR_CHECK(rmt_config(&config));
    ESP_ERROR_CHECK(rmt_driver_install(config.channel, 0, 0));

    // install ws2812 driver
    led_strip_config_t strip_config =
            LED_STRIP_DEFAULT_CONFIG(LED_STRIP_NUM_RGB_LEDS, (led_strip_dev_t)config.channel);
    _strip = led_strip_new_rmt_ws2812(&strip_config);
    if (!_strip) {
        ESP_LOGE(TAG, "install WS2812 driver failed");
    }
    ws2812_led_strip_set_all_rgb(BLACK);
    ws2812_led_strip_display();
}

void ws2812_led_strip_set_led(int led_index, uint32_t r, uint32_t g, uint32_t b) {
    esp_err_t err = _strip->set_pixel(_strip, led_index, r, g, b);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "set_pixel err: %d (%s)", err, esp_err_to_name(err));
    }
}

void ws2812_led_strip_display(void) {
    esp_err_t err = _strip->refresh(_strip, 100);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "refresh err: %d (%s)", err, esp_err_to_name(err));
    }
}

void ws2812_led_strip_set_led_rgb(int led_index, const led_strip_rgb_t rgb) {
    ws2812_led_strip_set_led(led_index, rgb.r, rgb.g, rgb.b);
}

void ws2812_led_strip_set_all(uint32_t r, uint32_t g, uint32_t b) {
    for (int i = 0; i < LED_STRIP_NUM_RGB_LEDS; i++) {
        ws2812_led_strip_set_led(i, r, g, b);
    }
}

void ws2812_led_strip_set_all_rgb(const led_strip_rgb_t rgb) {
    ws2812_led_strip_set_all(rgb.r, rgb.g, rgb.b);
}

void ws2812_led_strip_set_all_rgb_immediate(const led_strip_rgb_t rgb) {
    ws2812_led_strip_set_all_rgb(rgb);
    ws2812_led_strip_display();
}
