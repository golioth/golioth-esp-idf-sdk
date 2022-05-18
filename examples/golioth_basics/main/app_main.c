#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "golioth.h"

#define TAG "golioth_basics"

void on_ldb_setting(const char* path, const uint8_t* payload, size_t payload_size, void* arg) {
    int32_t value = golioth_payload_as_int(payload, payload_size);
    ESP_LOGI(TAG, "Got setting value = %d", value);
}

void on_ldb_root(const char* path, const uint8_t* payload, size_t payload_size, void* arg) {
    // TODO - parse with cJSON
}

void app_main(void) {
    // Initialization required for connecting to WiFi
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Connect to wifi
    // See $ENV{IDF_PATH}/examples/protocols/README.md for details
    ESP_ERROR_CHECK(example_connect());

    const char* psk_id = CONFIG_GOLIOTH_EXAMPLE_COAP_PSK_ID;
    const char* psk = CONFIG_GOLIOTH_EXAMPLE_COAP_PSK;
    golioth_client_t client = golioth_client_create(psk_id, psk);
    assert(client);

    golioth_lightdb_observe(client, "setting", on_ldb_setting, NULL);

    int iteration = 0;
    while (1) {
        golioth_log_info(client, TAG, "this is a message");
        golioth_lightdb_set_int(client, "iteration", iteration);
        golioth_lightdb_get(client, "", on_ldb_root, NULL);
        golioth_lightdb_delete(client, "delete_me");

        ESP_LOGI(TAG, "app_main delaying for 10s...");
        vTaskDelay(10000 / portTICK_PERIOD_MS);
        iteration++;
    };
}
