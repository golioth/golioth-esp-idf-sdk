#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "golioth.h"

#define TAG "golioth_basics"

void app_main(void) {
    // Initialization required for connecting to WiFi
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Connect to wifi
    // See $ENV{IDF_PATH}/examples/protocols/README.md for details
    ESP_ERROR_CHECK(example_connect());

    golioth_client_t client = golioth_client_create(
            CONFIG_GOLIOTH_EXAMPLE_COAP_PSK_ID,
            CONFIG_GOLIOTH_EXAMPLE_COAP_PSK);
    assert(client);

    golioth_lightdb_observe(client, ".d/setting");

    int iteration = 0;
    char iteration_str[16] = {};
    while (1) {
        const char* json_log = "{\"level\":\"info\",\"module\":\"example\",\"msg\":\"test\"}";
        golioth_log(client, json_log);

        sprintf(iteration_str, "%d", iteration);
        size_t iteration_str_len = strlen(iteration_str);
        golioth_lightdb_set(client, ".d/iteration", (const uint8_t*)iteration_str, iteration_str_len);

        golioth_lightdb_get(client, ".d");
        golioth_lightdb_get(client, ".d/nonexistant");

        golioth_lightdb_delete(client, ".d/delete_me");

        uint32_t free_heap = xPortGetFreeHeapSize();
        uint32_t min_free_heap = xPortGetMinimumEverFreeHeapSize();
        ESP_LOGI(TAG, "Free heap = %u bytes, Min ever free heap = %u", free_heap, min_free_heap);
        ESP_LOGI(TAG, "app_main delaying for 10s...");
        vTaskDelay(10000 / portTICK_PERIOD_MS);
        iteration++;
    };
}
