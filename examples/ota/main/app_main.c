#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_ota_ops.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "golioth.h"

#define TAG "example_ota"

static golioth_ota_manifest_t _ota_manifest;
static SemaphoreHandle_t _start_ota_sem;
static SemaphoreHandle_t _rollback_decision_sem;
static TimerHandle_t _rollback_timer;
static bool _should_rollback;
static uint8_t _ota_block_buffer[GOLIOTH_OTA_BLOCKSIZE + 1];
static const char* _current_version = "1.2.3";

static void on_ota_manifest(golioth_client_t client, const char* path, const uint8_t* payload, size_t payload_size, void* arg) {
    golioth_status_t status = golioth_ota_payload_as_manifest(payload, payload_size, &_ota_manifest);
    if (status != GOLIOTH_OK) {
        ESP_LOGE(TAG, "Failed to parse manifest: %s", golioth_status_to_str(status));
        return;
    }

    if (_ota_manifest.num_components == 0) {
        ESP_LOGI(TAG, "Manifest components array empty");
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

static bool ota_is_pending_verify() {
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            return true;
        }
    }
    return false;
}

static void on_rollback_timer(TimerHandle_t timer) {
    ESP_LOGW(TAG, "Rollback timer expired");
    _should_rollback = true;
    xSemaphoreGive(_rollback_decision_sem);
}

static void start_rollback_timer() {
    _rollback_timer = xTimerCreate(
            "rollback",
            30000 / portTICK_PERIOD_MS,
            pdFALSE, // auto-reload
            NULL, // pvTimerID
            on_rollback_timer);
    xTimerStart(_rollback_timer, 0);
}

static void stop_rollback_timer() {
    xTimerStop(_rollback_timer, 0);
}

static void on_client_event(golioth_client_t client, golioth_client_event_t event, void* arg) {
    ESP_LOGI(TAG, "Golioth client %s",
            event == GOLIOTH_CLIENT_EVENT_CONNECTED ? "connected" : "disconnected");
    if (event == GOLIOTH_CLIENT_EVENT_CONNECTED && ota_is_pending_verify()) {
        // We're connected to Golioth, which means everything should be okay with
        // the new image. We don't need to roll back.
        _should_rollback = false;
        xSemaphoreGive(_rollback_decision_sem);
    }
}

static bool header_valid(const uint8_t* bytes, size_t nbytes) {
    size_t header_size =
        sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);
    assert(nbytes >= header_size);

    esp_app_desc_t new_app_info;
    memcpy(
            &new_app_info,
            &bytes[
                sizeof(esp_image_header_t) +
                sizeof(esp_image_segment_header_t)],
            sizeof(esp_app_desc_t));

    esp_app_desc_t running_app_info;
    esp_ota_get_partition_description(esp_ota_get_running_partition(), &running_app_info);

    esp_app_desc_t invalid_app_info;
    const esp_partition_t* last_invalid_app = esp_ota_get_last_invalid_partition();
    esp_ota_get_partition_description(last_invalid_app, &invalid_app_info);

    // check current version with last invalid partition
    if (last_invalid_app) {
        if (memcmp(invalid_app_info.version, new_app_info.version, sizeof(new_app_info.version)) == 0) {
            ESP_LOGW(TAG, "New version is the same as invalid version.");
            ESP_LOGW(TAG, "Previously, there was an attempt to launch the firmware with %s version, but it failed.", invalid_app_info.version);
            ESP_LOGW(TAG, "The firmware has been rolled back to the previous version.");
            return false;
        }
    }

    return true;
}

void app_main(void) {
    esp_ota_handle_t update_handle = 0;
    esp_err_t err = ESP_OK;

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

    _start_ota_sem = xSemaphoreCreateBinary();
    _rollback_decision_sem = xSemaphoreCreateBinary();

    ESP_LOGI(TAG, "Current firmware version = %s", _current_version);

    golioth_client_register_event_callback(client, on_client_event, NULL);

    if (ota_is_pending_verify()) {
        ESP_LOGI(TAG, "State = Updating");
        golioth_ota_report_state(
                client,
                GOLIOTH_OTA_STATE_UPDATING,
                GOLIOTH_OTA_REASON_READY,
                "main",
                _current_version,
                NULL);

        // Start a one-shot timer that, upon expiration, will cause
        // a rollback and reboot.
        //
        // The timer will be stopped (and rollback cancelled) if we can
        // succesfully connect to Golioth.
        start_rollback_timer();

        ESP_LOGI(TAG, "Wait for Golioth connection (or timeout) before deciding to roll back or not");
        xSemaphoreTake(_rollback_decision_sem, portMAX_DELAY);
        if (_should_rollback) {
            ESP_LOGW(TAG, "Failed to connect to Golioth");
            ESP_LOGW(TAG, "!!!");
            ESP_LOGW(TAG, "!!! Rolling back and rebooting now!");
            ESP_LOGW(TAG, "!!!");
            // Note: since we're not connected, we can't report state to Golioth
            esp_ota_mark_app_invalid_rollback_and_reboot();
        } else {
            stop_rollback_timer();
            ESP_LOGI(TAG, "State = Idle");
            golioth_ota_report_state(
                    client,
                    GOLIOTH_OTA_STATE_UPDATING,
                    GOLIOTH_OTA_REASON_FIRMWARE_UPDATED_SUCCESSFULLY,
                    "main",
                    _current_version,
                    NULL);
            ESP_LOGI(TAG, "Firmware updated successfully!");
        }
        goto end;
    }

    golioth_ota_report_state(
            client,
            GOLIOTH_OTA_STATE_IDLE,
            GOLIOTH_OTA_REASON_READY,
            "main",
            _current_version,
            NULL);

    ESP_LOGI(TAG, "Waiting to receive OTA manifest with new firmware version");
    golioth_ota_observe_manifest(client, on_ota_manifest, NULL);
    xSemaphoreTake(_start_ota_sem, portMAX_DELAY);
    ESP_LOGI(TAG, "Received OTA manifest");

    const golioth_ota_component_t* main_component = golioth_ota_find_component(&_ota_manifest, "main");
    assert(main_component);
    ESP_LOGI(TAG, "Current version = %s, Target version = %s", _current_version, main_component->version);

    ESP_LOGI(TAG, "State = Downloading");
    golioth_ota_report_state(
            client,
            GOLIOTH_OTA_STATE_DOWNLOADING,
            GOLIOTH_OTA_REASON_READY,
            "main",
            _current_version,
            main_component->version);

    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
    assert(update_partition);
    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x",
             update_partition->subtype, update_partition->address);

    // Handle blocks one at a time
    ESP_LOGI(TAG, "Image size = %zu", main_component->size);
    size_t nblocks = golioth_ota_size_to_nblocks(main_component->size);
    size_t bytes_written = 0;
    for (size_t i = 0; i < nblocks; i++) {
        size_t block_nbytes = 0;
        ESP_LOGI(TAG, "Getting block index %d (%d/%d)", i, i + 1, nblocks);
        golioth_status_t status = golioth_ota_get_block(
                client,
                main_component->package,
                main_component->version,
                i,
                _ota_block_buffer,
                &block_nbytes);
        if (status != GOLIOTH_OK) {
            ESP_LOGE(TAG, "Failed to get block index %d (%s)", i, golioth_status_to_str(status));
            break;
        }

        assert(block_nbytes <= GOLIOTH_OTA_BLOCKSIZE);
        ESP_LOGD(TAG, "Got block index %d, nbytes %zu, 0x%02X", i, block_nbytes, _ota_block_buffer[0]);

        if (i == 0) {
            if (!header_valid(_ota_block_buffer, block_nbytes)) {
                break;
            }
            ESP_LOGI(TAG, "Erasing flash");
            err = esp_ota_begin(update_partition, main_component->size, &update_handle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
                esp_ota_abort(update_handle);
                break;
            }
        }

        err = esp_ota_write(update_handle, (const void *)_ota_block_buffer, block_nbytes);
        if (err != ESP_OK) {
            esp_ota_abort(update_handle);
            break;
        }

        bytes_written += block_nbytes;
    }

    ESP_LOGI(TAG, "Total bytes written: %zu", bytes_written);
    if (bytes_written != main_component->size) {
        ESP_LOGI(TAG, "Download interrupted, wrote %zu of %zu bytes",
                bytes_written, main_component->size);
        ESP_LOGI(TAG, "State = Idle");
        golioth_ota_report_state(
                client,
                GOLIOTH_OTA_STATE_IDLE,
                GOLIOTH_OTA_REASON_FIRMWARE_UPDATE_FAILED,
                "main",
                _current_version,
                main_component->version);
        esp_ota_abort(update_handle);
        goto end;
    }

    // Validate image
    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed, image is corrupted");
        } else {
            ESP_LOGE(TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
        }

        ESP_LOGI(TAG, "State = Idle");
        golioth_ota_report_state(
                client,
                GOLIOTH_OTA_STATE_IDLE,
                GOLIOTH_OTA_REASON_INTEGRITY_CHECK_FAILURE,
                "main",
                _current_version,
                main_component->version);
        goto end;
    }

    ESP_LOGI(TAG, "State = Downloaded");
    golioth_ota_report_state(
            client,
            GOLIOTH_OTA_STATE_DOWNLOADED,
            GOLIOTH_OTA_REASON_READY,
            "main",
            _current_version,
            main_component->version);

    ESP_LOGI(TAG, "State = Updating");
    golioth_ota_report_state(
            client,
            GOLIOTH_OTA_STATE_UPDATING,
            GOLIOTH_OTA_REASON_READY,
            "main",
            _current_version,
            NULL);

    ESP_LOGI(TAG, "Setting boot partition");
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
        goto end;
    }

    ESP_LOGI(TAG, "Rebooting into new image");
    esp_restart();

end:
    ESP_LOGI(TAG, "End of ota example");
    golioth_client_destroy(client);
    if (update_handle) {
        esp_ota_end(update_handle);
    }
    if (_start_ota_sem) {
        vSemaphoreDelete(_start_ota_sem);
    }
    if (_rollback_decision_sem) {
        vSemaphoreDelete(_rollback_decision_sem);
    }
}
