#include <string.h>
#include <esp_log.h>
#include <cJSON.h>
#include "golioth_ota.h"
#include "golioth_coap_client.h"

#define TAG "golioth_ota"

#define GOLIOTH_OTA_MANIFEST_PATH ".u/desired"
#define GOLIOTH_OTA_COMPONENT_PATH_PREFIX ".u/c/"

typedef struct {
    uint8_t* buf;
    size_t* block_nbytes;
} block_get_output_params_t;

size_t golioth_ota_size_to_nblocks(size_t component_size) {
    size_t nblocks = component_size / GOLIOTH_OTA_BLOCKSIZE;
    if ((component_size % GOLIOTH_OTA_BLOCKSIZE) != 0) {
        nblocks++;
    }
    return nblocks;
}

const golioth_ota_component_t* golioth_ota_find_component(const golioth_ota_manifest_t* manifest, const char* package) {
    // Scan the manifest until we find the component with matching package.
    const golioth_ota_component_t* found = NULL;
    for (int i = 0; i < manifest->num_components; i++) {
        const golioth_ota_component_t* c = &manifest->components[i];
        bool matches = (0 == strcmp(c->package, package));
        if (matches) {
            found = c;
            break;
        }
    }
    return found;
}

golioth_status_t golioth_ota_observe_manifest(
        golioth_client_t client,
        golioth_get_cb_fn callback,
        void* arg) {
    return golioth_coap_client_observe_async(
            client,
            "",
            GOLIOTH_OTA_MANIFEST_PATH,
            COAP_MEDIATYPE_APPLICATION_JSON,
            callback,
            arg);
}

golioth_status_t golioth_ota_report_state(
        golioth_client_t client,
        golioth_ota_state_t state,
        golioth_ota_reason_t reason,
        const char* package,
        const char* current_version,
        const char* target_version) {
    char jsonbuf[128] = {};
    cJSON* json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "state", state);
    cJSON_AddNumberToObject(json, "reason", reason);
    cJSON_AddStringToObject(json, "package", package);
    if (current_version) {
        cJSON_AddStringToObject(json, "version", current_version);
    }
    if (target_version) {
        cJSON_AddStringToObject(json, "target", target_version);
    }
    bool printed = cJSON_PrintPreallocated(json, jsonbuf, sizeof(jsonbuf) - 5, false);
    assert(printed);
    cJSON_Delete(json);

    return golioth_coap_client_set(
            client,
            GOLIOTH_OTA_COMPONENT_PATH_PREFIX,
            package,
            COAP_MEDIATYPE_APPLICATION_JSON,
            (const uint8_t*)jsonbuf,
            strlen(jsonbuf),
            true);
}

golioth_status_t golioth_ota_payload_as_manifest(
        const uint8_t* payload,
        size_t payload_len,
        golioth_ota_manifest_t* manifest) {
    golioth_status_t ret = GOLIOTH_OK;

    cJSON* json = cJSON_ParseWithLength((const char*)payload, payload_len);
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse manifest");
        ret = GOLIOTH_ERR_INVALID_FORMAT;
        goto cleanup;
    }

    const cJSON* seqnum = cJSON_GetObjectItemCaseSensitive(json, "sequenceNumber");
    if (!seqnum || !cJSON_IsNumber(seqnum)) {
        ESP_LOGE(TAG, "Key sequenceNumber not found");
        ret = GOLIOTH_ERR_INVALID_FORMAT;
        goto cleanup;
    }
    manifest->seqnum = seqnum->valueint;

    cJSON* components = cJSON_GetObjectItemCaseSensitive(json, "components");
    cJSON* component = NULL;
    cJSON_ArrayForEach(component, components) {
        golioth_ota_component_t* c = &manifest->components[manifest->num_components++];

        const cJSON* package = cJSON_GetObjectItemCaseSensitive(component, "package");
        if (!package || !cJSON_IsString(package)) {
            ESP_LOGE(TAG, "Key package not found");
            ret = GOLIOTH_ERR_INVALID_FORMAT;
            goto cleanup;
        }
        strncpy(c->package, package->valuestring, CONFIG_GOLIOTH_OTA_MAX_PACKAGE_NAME_LEN);

        const cJSON* version = cJSON_GetObjectItemCaseSensitive(component, "version");
        if (!version || !cJSON_IsString(version)) {
            ESP_LOGE(TAG, "Key version not found");
            ret = GOLIOTH_ERR_INVALID_FORMAT;
            goto cleanup;
        }
        strncpy(c->version, version->valuestring, CONFIG_GOLIOTH_OTA_MAX_VERSION_LEN);

        const cJSON* size = cJSON_GetObjectItemCaseSensitive(component, "size");
        if (!size || !cJSON_IsNumber(size)) {
            ESP_LOGE(TAG, "Key size not found");
            ret = GOLIOTH_ERR_INVALID_FORMAT;
            goto cleanup;
        }
        c->size = size->valueint;
    }

cleanup:
    if (json) {
        cJSON_Delete(json);
    }
    return ret;
}

static void on_block_rcvd(
        golioth_client_t client,
        const char* path,
        const uint8_t* payload,
        size_t payload_size,
        void* arg) {
    assert(arg);
    assert(payload_size <= GOLIOTH_OTA_BLOCKSIZE);
    block_get_output_params_t* out_params = (block_get_output_params_t*)arg;
    if (out_params->buf) {
        memcpy(out_params->buf, payload, payload_size);
    }
    if (out_params->block_nbytes) {
        *out_params->block_nbytes = payload_size;
    }
}

golioth_status_t golioth_ota_get_block(
        golioth_client_t client,
        const char* package,
        const char* version,
        size_t block_index,
        uint8_t* buf,  // must be at least GOLIOTH_OTA_BLOCKSIZE bytes
        size_t* block_nbytes) {
    char path[CONFIG_GOLIOTH_OTA_MAX_PACKAGE_NAME_LEN + CONFIG_GOLIOTH_OTA_MAX_VERSION_LEN + 2] = {};
    snprintf(path, sizeof(path), "%s@%s", package, version);
    block_get_output_params_t out_params = {
        .buf = buf,
        .block_nbytes = block_nbytes,
    };

    golioth_status_t status = GOLIOTH_OK;
    status = golioth_coap_client_get_block(
            client,
            GOLIOTH_OTA_COMPONENT_PATH_PREFIX,
            path,
            COAP_MEDIATYPE_APPLICATION_JSON,
            block_index,
            GOLIOTH_OTA_BLOCKSIZE,
            on_block_rcvd,
            &out_params,
            true);
    return status;
}
