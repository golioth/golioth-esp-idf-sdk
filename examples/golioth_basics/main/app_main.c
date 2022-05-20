#include <string.h>
#include <cJSON.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "golioth.h"

#define TAG "golioth_basics"

static void on_float_value(golioth_client_t client, const char* path, const uint8_t* payload, size_t payload_size, void* arg) {
    float value = golioth_payload_as_float(payload, payload_size);
    ESP_LOGI(TAG, "Got float_value = %f", value);
}

static void on_my_setting(golioth_client_t client, const char* path, const uint8_t* payload, size_t payload_size, void* arg) {
    // Payload might be null if desired/my_setting is deleted, so ignore that case
    if (golioth_payload_is_null(payload, payload_size)) {
        return;
    }

    int32_t* actual_value_ptr = (int32_t*)arg;
    int32_t desired_value = golioth_payload_as_int(payload, payload_size);
    ESP_LOGI(TAG, "Cloud desires my_setting = %d. Setting now.", desired_value);
    *actual_value_ptr = desired_value;
    golioth_lightdb_delete(client, path);
}

static void example_json_set(golioth_client_t client) {
    char jsonbuf[128] = {};
    cJSON* json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "a_float", 3.5f);
    cJSON_AddNumberToObject(json, "an_int", -5);
    cJSON_AddStringToObject(json, "a_string", "string_value");
    cJSON_AddBoolToObject(json, "a_bool", true);
    bool printed = cJSON_PrintPreallocated(json, jsonbuf, sizeof(jsonbuf) - 5, false);
    assert(printed);
    cJSON_Delete(json);
    golioth_lightdb_set_json(client, "example_json", jsonbuf, strlen(jsonbuf));
}

static void on_example_json(golioth_client_t client, const char* path, const uint8_t* payload, size_t payload_size, void* arg) {
    cJSON* json = cJSON_ParseWithLength((const char*)payload, payload_size);
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse example_json");
        return;
    }
    const cJSON* a_float = cJSON_GetObjectItemCaseSensitive(json, "a_float");
    if (cJSON_IsNumber(a_float)) {
        ESP_LOGI(TAG, "Got a_float = %f", a_float->valuedouble);
    }
    const cJSON* a_string = cJSON_GetObjectItemCaseSensitive(json, "a_string");
    if (cJSON_IsString(a_string) && (a_string->valuestring != NULL)) {
        ESP_LOGI(TAG, "Got a_string = %s", a_string->valuestring);
    }
    cJSON_Delete(json);
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

    int32_t iteration = 0;
    bool bool_toggle = false;
    float float_value = 1.234f;
    const char* string_value = "a string";

    // The cloud can request to set the value of my_setting via path "desired/my_setting"
    // If that happens, we will update the value and delete the path to indicate
    // the desired setting was applied.
    int32_t my_setting = 0;
    golioth_lightdb_observe(client, "desired/my_setting", on_my_setting, &my_setting);

    while (1) {
        // Synchronous API (blocks until server responds or times out)
        ESP_LOGI(TAG, "Start of synchronous example");
        golioth_log_info_sync(client, TAG, "This is a synchronous message");
        ESP_LOGI(TAG, "End of synchronous example");

        // Asynchronous API (non-blocking, doesn't wait for server response)
        golioth_log_info(client, TAG, "This is a message");
        golioth_lightdb_set_int(client, "iteration", iteration);
        golioth_lightdb_set_int(client, "my_setting", my_setting);
        golioth_lightdb_set_bool(client, "bool_toggle", bool_toggle);
        golioth_lightdb_set_float(client, "float_value", float_value);
        golioth_lightdb_set_string(client, "string_value", string_value, strlen(string_value));
        golioth_lightdb_get(client, "float_value", on_float_value, NULL);
        example_json_set(client);
        golioth_lightdb_get(client, "example_json", on_example_json, NULL);

        iteration++;
        bool_toggle = !bool_toggle;

        ESP_LOGI(TAG, "app_main delaying for 10s...");
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    };
}
