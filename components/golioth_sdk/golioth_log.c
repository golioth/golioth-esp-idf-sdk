#include <esp_log.h>
#include "golioth_coap_client.h"
#include "golioth_log.h"
#include "golioth_stats.h"

#define TAG "golioth_log"

#define CONFIG_GOLIOTH_LOG_MAX_MESSAGE_LEN 100

#if 0
golioth_status_t golioth_log(golioth_client_t client, const char* log_message) {
    golioth_coap_client_t* c = (golioth_coap_client_t*)client;
    if (!c) {
        return GOLIOTH_ERR_NULL;
    }

    ESP_LOGD(TAG, "PUT \"logs\"");
    size_t msg_len = strnlen(log_message, CONFIG_GOLIOTH_LOG_MAX_MESSAGE_LEN);

    // Memory will be free'd by the client task after being handled
    uint8_t* payload = (uint8_t*)calloc(1, msg_len);
    if (!payload) {
        ESP_LOGE(TAG, "Payload alloc failure");
        return GOLIOTH_ERR_MEM_ALLOC;
    }
    g_golioth_stats.total_allocd_bytes += msg_len;

    memcpy(payload, log_message, msg_len);

    golioth_coap_request_msg_t request_msg = {
        .type = GOLIOTH_COAP_REQUEST_PUT,
        .put = {
            .path = "logs",
            .content_type = COAP_MEDIATYPE_APPLICATION_JSON,
            .payload = payload,
            .payload_size = msg_len,
        },
    };
    BaseType_t sent = xQueueSend(c->request_queue, &request_msg, portMAX_DELAY);
    if (!sent) {
        ESP_LOGW(TAG, "Failed to enqueue request, queue full");
        return GOLIOTH_ERR_QUEUE_FULL;
    }

    return GOLIOTH_OK;
}
#endif

golioth_status_t golioth_log_error(golioth_client_t client, const char* tag, const char* log_message) {
    return GOLIOTH_OK;
}

golioth_status_t golioth_log_warn(golioth_client_t client, const char* tag, const char* log_message) {
    return GOLIOTH_OK;
}

golioth_status_t golioth_log_info(golioth_client_t client, const char* tag, const char* log_message) {
    return GOLIOTH_OK;
}

golioth_status_t golioth_log_debug(golioth_client_t client, const char* tag, const char* log_message) {
    return GOLIOTH_OK;
}
