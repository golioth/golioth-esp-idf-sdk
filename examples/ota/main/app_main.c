#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "golioth.h"

#define TAG "example_ota"

static golioth_ota_manifest_t _ota_manifest;
static SemaphoreHandle_t _start_ota_sem;
static uint8_t _ota_block_buffer[GOLIOTH_OTA_BLOCKSIZE];
static const char* _current_version = "1.2.3";

static void on_ota_manifest(golioth_client_t client, const char* path, const uint8_t* payload, size_t payload_size, void* arg) {
    golioth_status_t status = golioth_ota_payload_as_manifest(payload, payload_size, &_ota_manifest);
    if (status != GOLIOTH_OK) {
        ESP_LOGE(TAG, "Failed to parse manifest: %s", golioth_status_to_str(status));
        return;
    }

    const golioth_ota_component_t* main_component = golioth_ota_find_component(&_ota_manifest, "main");
    if (!main_component) {
        ESP_LOGE(TAG, "Did not find \"main\" component in manifest");
        return;
    }

    bool version_is_same = (0 == strcmp(_current_version, main_component->version));
    if (version_is_same) {
        ESP_LOGI(TAG, "Manifest version %s matches the current version. Nothing to do.", main_component->version);
        return;
    }

    // Version is different, so we should start OTA process.
    xSemaphoreGive(_start_ota_sem);
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


    // TODO - check if OTA completed successfully, report state, goto end

    golioth_ota_report_state(
            client,
            GOLIOTH_OTA_STATE_IDLE,
            GOLIOTH_OTA_REASON_READY,
            "main",
            _current_version,
            NULL);

    ESP_LOGI(TAG, "Waiting to receive OTA manifest with new firmware version");
    _start_ota_sem = xSemaphoreCreateBinary();
    golioth_ota_observe_manifest(client, on_ota_manifest, NULL);
    xSemaphoreTake(_start_ota_sem, portMAX_DELAY);
    vSemaphoreDelete(_start_ota_sem);
    ESP_LOGI(TAG, "Received OTA manifest. Starting OTA.");

    golioth_ota_report_state(
            client,
            GOLIOTH_OTA_STATE_DOWNLOADING,
            GOLIOTH_OTA_REASON_READY,
            "main",
            _current_version,
            NULL);

    const golioth_ota_component_t* main_component = golioth_ota_find_component(&_ota_manifest, "main");
    assert(main_component);

    // Handle blocks one at a time
    size_t nblocks = golioth_ota_size_to_nblocks(main_component->size);
    for (size_t i = 0; i < nblocks; i++) {
        size_t block_nbytes = 0;
        size_t offset = 0;
        ESP_LOGD(TAG, "Getting block index %d (%d/%d)", i, i + 1, nblocks);
        golioth_status_t status = golioth_ota_get_block(
                client,
                main_component->package,
                main_component->version,
                i,
                _ota_block_buffer,
                &block_nbytes,
                &offset);
        if (status != GOLIOTH_OK) {
            ESP_LOGE(TAG, "Failed to get block index %d (%s)", i, golioth_status_to_str(status));
            goto end;
        }

        assert(block_nbytes <= GOLIOTH_OTA_BLOCKSIZE);
        ESP_LOGD(TAG, "Got block index %d, nbytes %zu, offset %zu", i, block_nbytes, offset);

        // TODO - integrate with esp-idf OTA
    }

    golioth_ota_report_state(
            client,
            GOLIOTH_OTA_STATE_DOWNLOADED,
            GOLIOTH_OTA_REASON_READY,
            "main",
            _current_version,
            NULL);

    // TODO - apply
    golioth_ota_report_state(
            client,
            GOLIOTH_OTA_STATE_UPDATING,
            GOLIOTH_OTA_REASON_READY,
            "main",
            _current_version,
            NULL);

    // TODO - reboot

end:
    ESP_LOGI(TAG, "End of ota example");
    golioth_client_destroy(client);
}
