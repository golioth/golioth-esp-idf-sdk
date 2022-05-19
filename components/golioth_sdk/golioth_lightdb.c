#include <esp_log.h>
#include "golioth_coap_client.h"
#include "golioth_lightdb.h"
#include "golioth_stats.h"

#define TAG "golioth_lightdb"

#define GOLIOTH_LIGHTDB_PATH_PREFIX ".d/"

#if 0

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
    BaseType_t sent = xQueueSend(c->request_queue, &request_msg, 0);
    if (!sent) {
        ESP_LOGW(TAG, "Failed to enqueue request, queue full");
        return GOLIOTH_ERR_QUEUE_FULL;
    }

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
    g_golioth_stats.total_allocd_bytes += payload_size;

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

    BaseType_t sent = xQueueSend(c->request_queue, &request_msg, 0);
    if (!sent) {
        ESP_LOGW(TAG, "Failed to enqueue request, queue full");
        return GOLIOTH_ERR_QUEUE_FULL;
    }

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
    BaseType_t sent = xQueueSend(c->request_queue, &request_msg, 0);
    if (!sent) {
        ESP_LOGW(TAG, "Failed to enqueue request, queue full");
        return GOLIOTH_ERR_QUEUE_FULL;
    }

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

    BaseType_t sent = xQueueSend(c->request_queue, &request_msg, 0);
    if (!sent) {
        ESP_LOGW(TAG, "Failed to enqueue request, queue full");
        return GOLIOTH_ERR_QUEUE_FULL;
    }

    return GOLIOTH_OK;
}
#endif

golioth_status_t golioth_lightdb_set_int(
        golioth_client_t client,
        const char* path,
        int32_t value) {
    char buf[16] = {};
    snprintf(buf, sizeof(buf), "%d", value);
    return golioth_coap_client_set_async(
            client,
            GOLIOTH_LIGHTDB_PATH_PREFIX,
            path,
            COAP_MEDIATYPE_APPLICATION_JSON,
            (const uint8_t*)buf,
            strlen(buf));
}

golioth_status_t golioth_lightdb_set_bool(
        golioth_client_t client,
        const char* path,
        bool value) {
    const char* valuestr = (value ? "true" : "false");
    return golioth_coap_client_set_async(
            client,
            GOLIOTH_LIGHTDB_PATH_PREFIX,
            path,
            COAP_MEDIATYPE_APPLICATION_JSON,
            (const uint8_t*)valuestr,
            strlen(valuestr));
}

golioth_status_t golioth_lightdb_set_float(
        golioth_client_t client,
        const char* path,
        float value) {
    char buf[32] = {};
    snprintf(buf, sizeof(buf), "%f", value);
    return golioth_coap_client_set_async(
            client,
            GOLIOTH_LIGHTDB_PATH_PREFIX,
            path,
            COAP_MEDIATYPE_APPLICATION_JSON,
            (const uint8_t*)buf,
            strlen(buf));
}

golioth_status_t golioth_lightdb_set_string(
        golioth_client_t client,
        const char* path,
        const char* str,
        size_t str_len) {
    // Server requires that non-JSON-formatted strings
    // be surrounded with literal ".
    //
    // It's inefficient, but we're going to copy the string
    // so we can surround it in quotes.
    //
    // TODO - is there a better way to handle this?
    size_t bufsize = str_len + 3; // two " and a NULL
    char* buf = calloc(1, bufsize);
    if (!buf) {
        return GOLIOTH_ERR_MEM_ALLOC;
    }
    snprintf(buf, bufsize, "\"%s\"", str);

    golioth_status_t status = golioth_coap_client_set_async(
            client,
            GOLIOTH_LIGHTDB_PATH_PREFIX,
            path,
            COAP_MEDIATYPE_APPLICATION_JSON,
            (const uint8_t*)buf,
            bufsize - 1); // exluding NULL

    free(buf);
    return status;
}

golioth_status_t golioth_lightdb_delete(
        golioth_client_t client,
        const char* path) {
    return golioth_coap_client_delete_async(client, GOLIOTH_LIGHTDB_PATH_PREFIX, path);
}

golioth_status_t golioth_lightdb_get(
        golioth_client_t client,
        const char* path,
        golioth_get_cb_fn callback,
        void* arg) {
    return GOLIOTH_OK;
}

golioth_status_t golioth_lightdb_observe(
        golioth_client_t client,
        const char* path,
        golioth_get_cb_fn callback,
        void* arg) {
    return GOLIOTH_OK;
}



int32_t golioth_payload_as_int(const uint8_t* payload, size_t payload_size) {
    return strtol((const char*)payload, NULL, 10);
}

float golioth_payload_as_float(const uint8_t* payload, size_t payload_size) {
    return strtof((const char*)payload, NULL);
}
