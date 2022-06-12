#pragma once

#define LED_STRIP_NUM_RGB_LEDS 4

typedef struct {
    uint32_t r;
    uint32_t g;
    uint32_t b;
} led_strip_rgb_t;

const led_strip_rgb_t RED;
const led_strip_rgb_t BLACK;
const led_strip_rgb_t YELLOW;
const led_strip_rgb_t GREEN;
const led_strip_rgb_t BLUE;

void ws2812_led_strip_init(void);
void ws2812_led_strip_set_led(int led_index, uint32_t r, uint32_t g, uint32_t b);
void ws2812_led_strip_set_led_rgb(int led_index, const led_strip_rgb_t rgb);
void ws2812_led_strip_set_all(uint32_t r, uint32_t g, uint32_t b);
void ws2812_led_strip_set_all_rgb(const led_strip_rgb_t rgb);
void ws2812_led_strip_set_all_rgb_immediate(const led_strip_rgb_t rgb);
void ws2812_led_strip_display(void);
