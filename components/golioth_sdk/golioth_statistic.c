#include <assert.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include "golioth_statistic.h"

#define TAG "golioth_statistic"

static SemaphoreHandle_t _mutex;
static int32_t _stat_value[NUM_GOLIOTH_STATISTIC_IDS];

#define GENERATE_GOLIOTH_STATISTIC_STR(_, name) #name,
static const char* _stat_name[NUM_GOLIOTH_STATISTIC_IDS] = {
    FOREACH_GOLIOTH_STATISTIC(GENERATE_GOLIOTH_STATISTIC_STR)
};

static void lock() {
    if (!_mutex) {
        _mutex = xSemaphoreCreateMutex();
        assert(_mutex);
    }
    xSemaphoreTake(_mutex, portMAX_DELAY);
}

static void unlock() {
    xSemaphoreGive(_mutex);
}

void golioth_statistic_add(golioth_statistic_id_t id, int32_t value) {
    assert(id < NUM_GOLIOTH_STATISTIC_IDS);
    lock();
    _stat_value[id] += value;
    unlock();
}

void golioth_statistic_set(golioth_statistic_id_t id, int32_t value) {
    assert(id < NUM_GOLIOTH_STATISTIC_IDS);
    lock();
    _stat_value[id] = value;
    unlock();
}

void golioth_statistic_print_all(void) {
    lock();
    for (int i = 0; i < NUM_GOLIOTH_STATISTIC_IDS; i++) {
        ESP_LOGD(TAG, "%s = %d", _stat_name[i], _stat_value[i]);
    }
    unlock();
}
