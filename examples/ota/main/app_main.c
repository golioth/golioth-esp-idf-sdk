#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "golioth.h"
#include "fw_update.h"

#define TAG "example_ota"

static golioth_ota_manifest_t _ota_manifest;
static SemaphoreHandle_t _manifest_rcvd;
static SemaphoreHandle_t _golioth_connected;
static const char* _current_version = "1.2.3";

static void on_ota_manifest(golioth_client_t client, const char* path, const uint8_t* payload, size_t payload_size, void* arg) {
    golioth_status_t status = golioth_ota_payload_as_manifest(payload, payload_size, &_ota_manifest);
    if (status != GOLIOTH_OK) {
        ESP_LOGE(TAG, "Failed to parse manifest: %s", golioth_status_to_str(status));
        return;
    }
    xSemaphoreGive(_manifest_rcvd);
}

static void on_client_event(golioth_client_t client, golioth_client_event_t event, void* arg) {
    ESP_LOGI(TAG, "Golioth client %s",
            event == GOLIOTH_CLIENT_EVENT_CONNECTED ? "connected" : "disconnected");
    if (event == GOLIOTH_CLIENT_EVENT_CONNECTED) {
        xSemaphoreGive(_golioth_connected);
    }
}

void app_main(void) {
    // Initialization required for connecting to WiFi
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Connect to wifi
    // See $ENV{IDF_PATH}/examples/protocols/README.md for details
    ESP_ERROR_CHECK(example_connect());

    _manifest_rcvd = xSemaphoreCreateBinary();
    _golioth_connected = xSemaphoreCreateBinary();
    ESP_LOGI(TAG, "Current firmware version = %s", _current_version);

    const char* psk_id = CONFIG_GOLIOTH_EXAMPLE_COAP_PSK_ID;
    const char* psk = CONFIG_GOLIOTH_EXAMPLE_COAP_PSK;
    golioth_client_t client = golioth_client_create(psk_id, psk);
    assert(client);
    golioth_client_register_event_callback(client, on_client_event, NULL);

    fw_update_init(client, _current_version);

    // If it's the first time booting a new OTA image,
    // wait for successful connection to Golioth.
    //
    // If we don't connect after 30 seconds, roll back to the old image.
    if (fw_update_is_pending_verify()) {
        bool connected = xSemaphoreTake(_golioth_connected, 30000 / portMAX_DELAY);
        if (connected) {
            ESP_LOGI(TAG, "Firmware updated successfully!");
            fw_update_cancel_rollback();
            goto end;
        } else {
            // We didn't connect to Golioth cloud, so something might be wrong with
            // this firmware. Roll back and reboot.
            ESP_LOGW(TAG, "Failed to connect to Golioth");
            ESP_LOGW(TAG, "!!!");
            ESP_LOGW(TAG, "!!! Rolling back and rebooting now!");
            ESP_LOGW(TAG, "!!!");
            fw_update_rollback_and_reboot();
        }
    }

    golioth_ota_observe_manifest(client, on_ota_manifest, NULL);

    // Loop here until we get a manifest with a different firmware version
    while (1) {
        ESP_LOGI(TAG, "Waiting to receive OTA manifest");
        xSemaphoreTake(_manifest_rcvd, portMAX_DELAY);
        ESP_LOGI(TAG, "Received OTA manifest");
        if (fw_update_manifest_version_is_different(&_ota_manifest)) {
            break;
        } else {
            ESP_LOGI(TAG, "Manifest version matches the current version. Nothing to do.");
        }

    }

    if (fw_update_download_and_write_flash() != GOLIOTH_OK) {
        ESP_LOGE(TAG, "Firmware download failed");
        goto end;
    }

    if (fw_update_validate() != GOLIOTH_OK) {
        ESP_LOGE(TAG, "Firmware validate failed");
        goto end;
    }

    if (fw_update_change_boot_image() != GOLIOTH_OK) {
        ESP_LOGE(TAG, "Firmware change boot image failed");
        goto end;
    }

    ESP_LOGI(TAG, "Rebooting into new image");
    esp_restart();

end:
    ESP_LOGI(TAG, "End of ota example");
    fw_update_end();
    golioth_client_destroy(client);
    vSemaphoreDelete(_golioth_connected);
    vSemaphoreDelete(_manifest_rcvd);
}
