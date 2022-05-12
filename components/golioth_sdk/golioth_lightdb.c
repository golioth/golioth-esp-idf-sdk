#include <esp_log.h>
#include "golioth_coap_client.h"
#include "golioth_lightdb.h"
#include "golioth_statistic.h"

#define TAG "golioth_lightdb"

// TODO - The implementations here are not lightdb specific.
// It might be better to put this code in a file named golioth_coap_async.c,
// so it can be used across the higher-level services (like log, lightdb, etc).

golioth_status_t golioth_lightdb_observe(golioth_client_t client, const char* path) {
    golioth_coap_client_t* c = (golioth_coap_client_t*)client;
    if (!c) {
        return GOLIOTH_ERR_NULL;
    }

    ESP_LOGD(TAG, "OBSERVE \"%s\"", path);
    golioth_coap_request_msg_t request_msg = {
        .type = GOLIOTH_COAP_REQUEST_OBSERVE,
        .observe = {
            .path = path,
            .content_type = COAP_MEDIATYPE_APPLICATION_JSON,
        },
    };
    BaseType_t sent = xQueueSend(c->request_queue, &request_msg, portMAX_DELAY);
    assert(sent == pdTRUE);

    return GOLIOTH_OK;
}

golioth_status_t golioth_lightdb_set(
        golioth_client_t client,
        const char* path,
        const uint8_t* payload,
        size_t payload_size) {
    golioth_coap_client_t* c = (golioth_coap_client_t*)client;
    if (!c) {
        return GOLIOTH_ERR_NULL;
    }

    ESP_LOGD(TAG, "PUT \"%s\"", path);

    // Memory will be free'd by the client task after being handled
    uint8_t* payload_copy = (uint8_t*)calloc(1, payload_size);
    if (!payload_copy) {
        ESP_LOGE(TAG, "Payload alloc failure");
        return GOLIOTH_ERR_MEM_ALLOC;
    }
    golioth_statistic_add(GSTAT_ID_ALLOCATED_BYTES, payload_size);

    memcpy(payload_copy, payload, payload_size);

    golioth_coap_request_msg_t request_msg = {
        .type = GOLIOTH_COAP_REQUEST_PUT,
        .put = {
            .path = path,
            .content_type = COAP_MEDIATYPE_APPLICATION_JSON,
            .payload = payload_copy,
            .payload_size = payload_size,
        },
    };

    BaseType_t sent = xQueueSend(c->request_queue, &request_msg, portMAX_DELAY);
    assert(sent == pdTRUE);

    return GOLIOTH_OK;
}

golioth_status_t golioth_lightdb_get(golioth_client_t client, const char* path) {
    golioth_coap_client_t* c = (golioth_coap_client_t*)client;
    if (!c) {
        return GOLIOTH_ERR_NULL;
    }

    ESP_LOGD(TAG, "GET \"%s\"", path);
    golioth_coap_request_msg_t request_msg = {
        .type = GOLIOTH_COAP_REQUEST_GET,
        .observe = {
            .path = path,
            .content_type = COAP_MEDIATYPE_APPLICATION_JSON,
        },
    };
    BaseType_t sent = xQueueSend(c->request_queue, &request_msg, portMAX_DELAY);
    assert(sent == pdTRUE);

    return GOLIOTH_OK;
}

golioth_status_t golioth_lightdb_delete(golioth_client_t client, const char* path) {
    golioth_coap_client_t* c = (golioth_coap_client_t*)client;
    if (!c) {
        return GOLIOTH_ERR_NULL;
    }

    golioth_coap_request_msg_t request_msg = {
        .type = GOLIOTH_COAP_REQUEST_DELETE,
        .delete = {
            .path = path,
        },
    };

    BaseType_t sent = xQueueSend(c->request_queue, &request_msg, portMAX_DELAY);
    assert(sent == pdTRUE);

    return GOLIOTH_OK;
}
