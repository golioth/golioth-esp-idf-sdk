#include <esp_log.h>
#include "golioth_coap_client.h"
#include "golioth_log.h"

#define TAG "golioth_log"

#define CONFIG_GOLIOTH_LOG_MAX_MESSAGE_LEN 100

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
    assert(sent == pdTRUE);

    return GOLIOTH_OK;
}
