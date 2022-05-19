#include <esp_log.h>
#include <cJSON.h>
#include "golioth_coap_client.h"
#include "golioth_log.h"

#define TAG "golioth_log"

#define CONFIG_GOLIOTH_LOG_MAX_MESSAGE_LEN 100

static const char* _level_to_str[GOLIOTH_LOG_LEVEL_DEBUG + 1] = {
    [GOLIOTH_LOG_LEVEL_ERROR] = "error",
    [GOLIOTH_LOG_LEVEL_WARN] = "warn",
    [GOLIOTH_LOG_LEVEL_INFO] = "info",
    [GOLIOTH_LOG_LEVEL_DEBUG] = "debug"
};

golioth_status_t golioth_log_error(golioth_client_t client, const char* tag, const char* log_message) {
    return golioth_log(client, GOLIOTH_LOG_LEVEL_ERROR, tag, log_message);
}

golioth_status_t golioth_log_warn(golioth_client_t client, const char* tag, const char* log_message) {
    return golioth_log(client, GOLIOTH_LOG_LEVEL_WARN, tag, log_message);
}

golioth_status_t golioth_log_info(golioth_client_t client, const char* tag, const char* log_message) {
    return golioth_log(client, GOLIOTH_LOG_LEVEL_INFO, tag, log_message);
}

golioth_status_t golioth_log_debug(golioth_client_t client, const char* tag, const char* log_message) {
    return golioth_log(client, GOLIOTH_LOG_LEVEL_DEBUG, tag, log_message);
}

golioth_status_t golioth_log(golioth_client_t client, golioth_log_level_t level, const char* tag, const char* log_message) {
    assert(level <= GOLIOTH_LOG_LEVEL_DEBUG);

    char logbuf[CONFIG_GOLIOTH_LOG_MAX_MESSAGE_LEN + 5] = {};
    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "level", _level_to_str[level]);
    cJSON_AddStringToObject(json, "module", tag);
    cJSON_AddStringToObject(json, "msg", log_message);
    bool printed = cJSON_PrintPreallocated(json, logbuf, sizeof(logbuf), false);
    cJSON_Delete(json);

    if (!printed) {
        ESP_LOGE(TAG, "Failed to serialize log: %s", logbuf);
        return GOLIOTH_ERR_SERIALIZE;
    }

    size_t msg_len = strnlen(logbuf, CONFIG_GOLIOTH_LOG_MAX_MESSAGE_LEN);
    return golioth_coap_client_set_async(
            client,
            "", // path-prefix unused
            "logs",
            COAP_MEDIATYPE_APPLICATION_JSON,
            (const uint8_t*)logbuf,
            msg_len);
}
