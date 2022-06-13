#include "time.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_timer.h"

uint64_t millis() {
    int64_t time_us = esp_timer_get_time();
    if (time_us < 0) {
        time_us = 0;
    }
    uint64_t time_ms = (uint64_t)time_us / 1000;
    return time_ms;
}

void delay_ms(uint32_t ms) {
    vTaskDelay(ms / portTICK_PERIOD_MS);
}
