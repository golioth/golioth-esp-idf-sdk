/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include <esp_log.h>
#include <cJSON.h>
#include "golioth_coap_client.h"
#include "golioth_rpc.h"
#include "golioth_stats.h"
#include "golioth_util.h"

#define TAG "golioth_rpc"

#define GOLIOTH_RPC_PATH_PREFIX ".rpc/"

#define MAX_RPC_CALLBACKS 8
static golioth_rpc_cb_fn _rpc_callbacks[MAX_RPC_CALLBACKS];
static const char* _rpc_callback_methods[MAX_RPC_CALLBACKS];
static int _num_registered_rpc_callbacks;

golioth_status_t golioth_rpc_ack_internal(
        golioth_client_t client,
        const char* call_id,
        uint8_t status_code,
        uint8_t* detail,
        size_t detail_size,
        bool is_synchronous) {
    char buf[256] = {};
    if (detail_size > 0) {
        snprintf(buf, sizeof(buf), "{ \"id\": \"%s\", \"statusCode\": %d, \"detail\": %s }", call_id, status_code, detail);
    } else {
        snprintf(buf, sizeof(buf), "{ \"id\": \"%s\", \"statusCode\": %d }", call_id, status_code);
    }
    return golioth_coap_client_set(
            client,
            GOLIOTH_RPC_PATH_PREFIX,
            "status",            
            COAP_MEDIATYPE_APPLICATION_JSON,
            (const uint8_t*)buf,
            strlen(buf),
            is_synchronous);
}

golioth_status_t golioth_rpc_ack_async(
        golioth_client_t client,
        const char* call_id,
        uint8_t status_code,
        uint8_t* detail,
        size_t detail_size) {
    return golioth_rpc_ack_internal(client, call_id, status_code, detail, detail_size, false);
}

golioth_status_t golioth_rpc_ack_sync(
        golioth_client_t client,
        const char* call_id,
        uint8_t status_code,
        uint8_t* detail,
        size_t detail_size) {
    return golioth_rpc_ack_internal(client, call_id, status_code, detail, detail_size, true);
}

static void on_rpc(
        golioth_client_t client,
        const char* path,
        const uint8_t* payload,
        size_t payload_size,
        void* arg) {

    cJSON* json = cJSON_ParseWithLength((const char*)payload, payload_size);
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse rpc call");
        goto cleanup;
    }

    const cJSON* rpc_call_id = cJSON_GetObjectItemCaseSensitive(json, "id");
    if (!rpc_call_id || !cJSON_IsString(rpc_call_id)) {
        ESP_LOGE(TAG, "Key id not found");
        goto cleanup;
    }

    const cJSON* rpc_method = cJSON_GetObjectItemCaseSensitive(json, "method");
    if (!rpc_method || !cJSON_IsString(rpc_method)) {
        ESP_LOGE(TAG, "Key method not found");
        goto cleanup;
    }
    
    const cJSON* params = cJSON_GetObjectItemCaseSensitive(json, "params");
    if (!params) {
        ESP_LOGE(TAG, "Key params not found");
        goto cleanup;
    }

    uint8_t detail[64] = {};
    //TODO(fix): copy path internally on the SDK
    static char call_id[64] = {};
    strncpy(call_id, rpc_call_id->valuestring, 64);
    ESP_LOGI(TAG, "Calling RPC callback for call id :%s", call_id);
    bool method_found = false;
    for (int i = 0; i < _num_registered_rpc_callbacks; i++){
        if (strcmp(_rpc_callback_methods[i], rpc_method->valuestring) == 0){
            method_found = true;
            uint8_t status = _rpc_callbacks[i](client, rpc_method->valuestring, params, detail, 64);
            ESP_LOGI(TAG, "RPC status code %d for call id :%s", status, call_id);
            golioth_rpc_ack_async(client, call_id, status, detail, strlen((const char*)detail));
            break;
        }
    }
    if (!method_found) {
        golioth_rpc_ack_async(client, call_id, RPC_UNAVAILABLE, detail, 0);
    }

cleanup:
    if (json) {
        cJSON_Delete(json);
    }
}

golioth_status_t golioth_rpc_register(
        golioth_client_t client,
        const char* method,
        golioth_rpc_cb_fn callback) {                
    _rpc_callbacks[_num_registered_rpc_callbacks] = callback;
    _rpc_callback_methods[_num_registered_rpc_callbacks] = method;
    if (_num_registered_rpc_callbacks == 0) {
        _num_registered_rpc_callbacks++;
        return golioth_coap_client_observe_async(
            client,
            GOLIOTH_RPC_PATH_PREFIX,
            "",
            COAP_MEDIATYPE_APPLICATION_JSON,
            on_rpc,
            NULL);
    }
    _num_registered_rpc_callbacks++;
    return GOLIOTH_OK;
}


