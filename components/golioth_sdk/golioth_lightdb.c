#include <esp_log.h>
#include "golioth_coap_client.h"
#include "golioth_lightdb.h"
#include "golioth_stats.h"

#define TAG "golioth_lightdb"

#define GOLIOTH_LIGHTDB_PATH_PREFIX ".d/"

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
    return golioth_coap_client_get_async(
            client,
            GOLIOTH_LIGHTDB_PATH_PREFIX,
            path,
            COAP_MEDIATYPE_APPLICATION_JSON,
            callback,
            arg);
}

golioth_status_t golioth_lightdb_observe(
        golioth_client_t client,
        const char* path,
        golioth_get_cb_fn callback,
        void* arg) {
    return golioth_coap_client_observe_async(
            client,
            GOLIOTH_LIGHTDB_PATH_PREFIX,
            path,
            COAP_MEDIATYPE_APPLICATION_JSON,
            callback,
            arg);
}

int32_t golioth_payload_as_int(const uint8_t* payload, size_t payload_size) {
    return strtol((const char*)payload, NULL, 10);
}

float golioth_payload_as_float(const uint8_t* payload, size_t payload_size) {
    return strtof((const char*)payload, NULL);
}

bool golioth_payload_is_null(const uint8_t* payload, size_t payload_size) {
    if (!payload || payload_size == 0) {
        return true;
    }
    if (payload_size >= 4) {
        if (strncmp((const char*)payload, "null", 4) == 0) {
            return true;
        }
    }
    return false;
}
