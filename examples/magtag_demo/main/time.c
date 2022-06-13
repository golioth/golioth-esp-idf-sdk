#include "time.h"
#include "esp_timer.h"

uint64_t millis() {
    int64_t time_us = esp_timer_get_time();
    if (time_us < 0) {
        time_us = 0;
    }
    uint64_t time_ms = (uint64_t)time_us / 1000;
    return time_ms;
}
