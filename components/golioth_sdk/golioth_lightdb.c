#include <esp_log.h>
#include "golioth_coap_client.h"
#include "golioth_lightdb.h"
#include "golioth_stats.h"
#include "golioth_util.h"

#define TAG "golioth_lightdb"

#define GOLIOTH_LIGHTDB_STATE_PATH_PREFIX ".d/"
#define GOLIOTH_LIGHTDB_STREAM_PATH_PREFIX ".s/"

typedef enum {
    LIGHTDB_GET_TYPE_INT,
    LIGHTDB_GET_TYPE_BOOL,
    LIGHTDB_GET_TYPE_FLOAT,
    LIGHTDB_GET_TYPE_STRING,
} lightdb_get_type_t;

typedef struct {
    lightdb_get_type_t type;
    union {
        int32_t* i;
        float* f;
        bool* b;
        char* strbuf;
    };
    size_t strbuf_size;  // only applicable for string type
    bool is_null;
} lightdb_get_response_t;

static golioth_status_t golioth_lightdb_set_int_internal(
        golioth_client_t client,
        const char* path_prefix,
        const char* path,
        int32_t value,
        bool is_synchronous) {
    char buf[16] = {};
    snprintf(buf, sizeof(buf), "%d", value);
    return golioth_coap_client_set(
            client,
            path_prefix,
            path,
            COAP_MEDIATYPE_APPLICATION_JSON,
            (const uint8_t*)buf,
            strlen(buf),
            is_synchronous);
}

static golioth_status_t golioth_lightdb_set_bool_internal(
        golioth_client_t client,
        const char* path_prefix,
        const char* path,
        bool value,
        bool is_synchronous) {
    const char* valuestr = (value ? "true" : "false");
    return golioth_coap_client_set(
            client,
            path_prefix,
            path,
            COAP_MEDIATYPE_APPLICATION_JSON,
            (const uint8_t*)valuestr,
            strlen(valuestr),
            is_synchronous);
}

static golioth_status_t golioth_lightdb_set_float_internal(
        golioth_client_t client,
        const char* path_prefix,
        const char* path,
        float value,
        bool is_synchronous) {
    char buf[32] = {};
    snprintf(buf, sizeof(buf), "%f", value);
    return golioth_coap_client_set(
            client,
            path_prefix,
            path,
            COAP_MEDIATYPE_APPLICATION_JSON,
            (const uint8_t*)buf,
            strlen(buf),
            is_synchronous);
}

static golioth_status_t golioth_lightdb_set_string_internal(
        golioth_client_t client,
        const char* path_prefix,
        const char* path,
        const char* str,
        size_t str_len,
        bool is_synchronous) {
    // Server requires that non-JSON-formatted strings
    // be surrounded with literal ".
    //
    // It's inefficient, but we're going to copy the string
    // so we can surround it in quotes.
    //
    // TODO - is there a better way to handle this?
    size_t bufsize = str_len + 3;  // two " and a NULL
    char* buf = calloc(1, bufsize);
    if (!buf) {
        return GOLIOTH_ERR_MEM_ALLOC;
    }
    snprintf(buf, bufsize, "\"%s\"", str);

    golioth_status_t status = golioth_coap_client_set(
            client,
            path_prefix,
            path,
            COAP_MEDIATYPE_APPLICATION_JSON,
            (const uint8_t*)buf,
            bufsize - 1,  // exluding NULL
            is_synchronous);

    free(buf);
    return status;
}

static golioth_status_t golioth_lightdb_delete_internal(
        golioth_client_t client,
        const char* path_prefix,
        const char* path,
        bool is_synchronous) {
    return golioth_coap_client_delete(client, path_prefix, path, is_synchronous);
}

static golioth_status_t golioth_lightdb_get_internal(
        golioth_client_t client,
        const char* path_prefix,
        const char* path,
        golioth_get_cb_fn callback,
        void* arg,
        bool is_synchronous) {
    return golioth_coap_client_get(
            client,
            path_prefix,
            path,
            COAP_MEDIATYPE_APPLICATION_JSON,
            callback,
            arg,
            is_synchronous);
}

static golioth_status_t golioth_lightdb_set_json_internal(
        golioth_client_t client,
        const char* path_prefix,
        const char* path,
        const char* json_str,
        size_t json_str_len,
        bool is_synchronous) {
    return golioth_coap_client_set(
            client,
            path_prefix,
            path,
            COAP_MEDIATYPE_APPLICATION_JSON,
            (const uint8_t*)json_str,
            json_str_len,
            is_synchronous);
}

int32_t golioth_payload_as_int(const uint8_t* payload, size_t payload_size) {
    return strtol((const char*)payload, NULL, 10);
}

float golioth_payload_as_float(const uint8_t* payload, size_t payload_size) {
    return strtof((const char*)payload, NULL);
}

bool golioth_payload_as_bool(const uint8_t* payload, size_t payload_size) {
    if (payload_size < 4) {
        return false;
    }
    return (0 == strncmp((const char*)payload, "true", 4));
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

<<<<<<< Updated upstream
golioth_status_t
golioth_lightdb_set_int_async(golioth_client_t client, const char* path, int32_t value) {
    return golioth_lightdb_set_int_internal(
            client, GOLIOTH_LIGHTDB_STATE_PATH_PREFIX, path, value, false);
}

golioth_status_t
golioth_lightdb_set_bool_async(golioth_client_t client, const char* path, bool value) {
    return golioth_lightdb_set_bool_internal(
            client, GOLIOTH_LIGHTDB_STATE_PATH_PREFIX, path, value, false);
}

golioth_status_t
golioth_lightdb_set_float_async(golioth_client_t client, const char* path, float value) {
    return golioth_lightdb_set_float_internal(
            client, GOLIOTH_LIGHTDB_STATE_PATH_PREFIX, path, value, false);
}

golioth_status_t golioth_lightdb_set_string_async(
        golioth_client_t client,
        const char* path,
        const char* str,
        size_t str_len) {
    return golioth_lightdb_set_string_internal(
            client, GOLIOTH_LIGHTDB_STATE_PATH_PREFIX, path, str, str_len, false);
}

golioth_status_t golioth_lightdb_set_json_async(
        golioth_client_t client,
        const char* path,
        const char* json_str,
        size_t json_str_len) {
    return golioth_lightdb_set_json_internal(
            client, GOLIOTH_LIGHTDB_STATE_PATH_PREFIX, path, json_str, json_str_len, false);
}

golioth_status_t golioth_lightdb_get_async(
        golioth_client_t client,
        const char* path,
        golioth_get_cb_fn callback,
        void* arg) {
    return golioth_lightdb_get_internal(
            client, GOLIOTH_LIGHTDB_STATE_PATH_PREFIX, path, callback, arg, false);
=======
golioth_status_t golioth_lightdb_set_int(golioth_client_t client, const char* path, int32_t value) {
    return golioth_lightdb_set_int_internal(
            client, GOLIOTH_LIGHTDB_STATE_PATH_PREFIX, path, value, false);
}

golioth_status_t golioth_lightdb_set_bool(golioth_client_t client, const char* path, bool value) {
    return golioth_lightdb_set_bool_internal(
            client, GOLIOTH_LIGHTDB_STATE_PATH_PREFIX, path, value, false);
}

golioth_status_t golioth_lightdb_set_float(golioth_client_t client, const char* path, float value) {
    return golioth_lightdb_set_float_internal(
            client, GOLIOTH_LIGHTDB_STATE_PATH_PREFIX, path, value, false);
}

golioth_status_t golioth_lightdb_set_string(
        golioth_client_t client,
        const char* path,
        const char* str,
        size_t str_len) {
    return golioth_lightdb_set_string_internal(
            client, GOLIOTH_LIGHTDB_STATE_PATH_PREFIX, path, str, str_len, false);
}

golioth_status_t golioth_lightdb_set_json(
        golioth_client_t client,
        const char* path,
        const char* json_str,
        size_t json_str_len) {
    return golioth_lightdb_set_json_internal(
            client, GOLIOTH_LIGHTDB_STATE_PATH_PREFIX, path, json_str, json_str_len, false);
}

golioth_status_t golioth_lightdb_get(
        golioth_client_t client,
        const char* path,
        golioth_get_cb_fn callback,
        void* arg) {
    return golioth_lightdb_get_internal(
            client, GOLIOTH_LIGHTDB_STATE_PATH_PREFIX, path, callback, arg, false);
>>>>>>> Stashed changes
}

golioth_status_t golioth_lightdb_delete_async(golioth_client_t client, const char* path) {
    return golioth_lightdb_delete_internal(client, GOLIOTH_LIGHTDB_STATE_PATH_PREFIX, path, false);
}

golioth_status_t golioth_lightdb_observe_async(
        golioth_client_t client,
        const char* path,
        golioth_get_cb_fn callback,
        void* arg) {
    return golioth_coap_client_observe_async(
            client,
            GOLIOTH_LIGHTDB_STATE_PATH_PREFIX,
            path,
            COAP_MEDIATYPE_APPLICATION_JSON,
            callback,
            arg);
}


golioth_status_t
golioth_lightdb_set_int_sync(golioth_client_t client, const char* path, int32_t value) {
    return golioth_lightdb_set_int_internal(
            client, GOLIOTH_LIGHTDB_STATE_PATH_PREFIX, path, value, true);
}

golioth_status_t
golioth_lightdb_set_bool_sync(golioth_client_t client, const char* path, bool value) {
    return golioth_lightdb_set_bool_internal(
            client, GOLIOTH_LIGHTDB_STATE_PATH_PREFIX, path, value, true);
}

golioth_status_t
golioth_lightdb_set_float_sync(golioth_client_t client, const char* path, float value) {
    return golioth_lightdb_set_float_internal(
            client, GOLIOTH_LIGHTDB_STATE_PATH_PREFIX, path, value, true);
}

golioth_status_t golioth_lightdb_set_string_sync(
        golioth_client_t client,
        const char* path,
        const char* str,
        size_t str_len) {
    return golioth_lightdb_set_string_internal(
            client, GOLIOTH_LIGHTDB_STATE_PATH_PREFIX, path, str, str_len, true);
}

golioth_status_t golioth_lightdb_set_json_sync(
        golioth_client_t client,
        const char* path,
        const char* json_str,
        size_t json_str_len) {
    return golioth_lightdb_set_json_internal(
            client, GOLIOTH_LIGHTDB_STATE_PATH_PREFIX, path, json_str, json_str_len, true);
}

static void on_payload(
        golioth_client_t client,
        const char* path,
        const uint8_t* payload,
        size_t payload_size,
        void* arg) {
    lightdb_get_response_t* response = (lightdb_get_response_t*)arg;

    if (golioth_payload_is_null(payload, payload_size)) {
        response->is_null = true;
        return;
    }

    switch (response->type) {
        case LIGHTDB_GET_TYPE_INT:
            *response->i = golioth_payload_as_int(payload, payload_size);
            break;
        case LIGHTDB_GET_TYPE_FLOAT:
            *response->f = golioth_payload_as_float(payload, payload_size);
            break;
        case LIGHTDB_GET_TYPE_BOOL:
            *response->b = golioth_payload_as_bool(payload, payload_size);
            break;
        case LIGHTDB_GET_TYPE_STRING: {
            // Remove the leading and trailing quote to get the raw string value
            size_t nbytes = min(response->strbuf_size - 1, payload_size - 2);
            memcpy(response->strbuf, payload + 1 /* skip quote */, nbytes);
            response->strbuf[nbytes] = 0;
        } break;
        default:
            assert(false);
    }
}

golioth_status_t
golioth_lightdb_get_int_sync(golioth_client_t client, const char* path, int32_t* value) {
    lightdb_get_response_t response = {
            .type = LIGHTDB_GET_TYPE_INT,
            .i = value,
    };
    golioth_status_t status = golioth_lightdb_get_internal(
            client, GOLIOTH_LIGHTDB_STATE_PATH_PREFIX, path, on_payload, &response, true);
    if (response.is_null) {
        return GOLIOTH_ERR_NULL;
    }
    return status;
}

golioth_status_t
golioth_lightdb_get_bool_sync(golioth_client_t client, const char* path, bool* value) {
    lightdb_get_response_t response = {
            .type = LIGHTDB_GET_TYPE_BOOL,
            .b = value,
    };
    golioth_status_t status = golioth_lightdb_get_internal(
            client, GOLIOTH_LIGHTDB_STATE_PATH_PREFIX, path, on_payload, &response, true);
    if (response.is_null) {
        return GOLIOTH_ERR_NULL;
    }
    return status;
}

golioth_status_t
golioth_lightdb_get_float_sync(golioth_client_t client, const char* path, float* value) {
    lightdb_get_response_t response = {
            .type = LIGHTDB_GET_TYPE_FLOAT,
            .f = value,
    };
    golioth_status_t status = golioth_lightdb_get_internal(
            client, GOLIOTH_LIGHTDB_STATE_PATH_PREFIX, path, on_payload, &response, true);
    if (response.is_null) {
        return GOLIOTH_ERR_NULL;
    }
    return status;
}

golioth_status_t golioth_lightdb_get_string_sync(
        golioth_client_t client,
        const char* path,
        char* strbuf,
        size_t strbuf_size) {
    lightdb_get_response_t response = {
            .type = LIGHTDB_GET_TYPE_STRING,
            .strbuf = strbuf,
            .strbuf_size = strbuf_size,
    };
    golioth_status_t status = golioth_lightdb_get_internal(
            client, GOLIOTH_LIGHTDB_STATE_PATH_PREFIX, path, on_payload, &response, true);
    if (response.is_null) {
        return GOLIOTH_ERR_NULL;
    }
    return status;
}

golioth_status_t golioth_lightdb_get_json_sync(
        golioth_client_t client,
        const char* path,
        char* strbuf,
        size_t strbuf_size) {
    return golioth_lightdb_get_string_sync(client, path, strbuf, strbuf_size);
}

golioth_status_t golioth_lightdb_delete_sync(golioth_client_t client, const char* path) {
    return golioth_lightdb_delete_internal(client, GOLIOTH_LIGHTDB_STATE_PATH_PREFIX, path, true);
}

golioth_status_t
golioth_lightdb_stream_set_int_async(golioth_client_t client, const char* path, int32_t value) {
    return golioth_lightdb_set_int_internal(
            client, GOLIOTH_LIGHTDB_STREAM_PATH_PREFIX, path, value, false);
}

golioth_status_t
golioth_lightdb_stream_set_bool_async(golioth_client_t client, const char* path, bool value) {
    return golioth_lightdb_set_bool_internal(
            client, GOLIOTH_LIGHTDB_STREAM_PATH_PREFIX, path, value, false);
}

golioth_status_t
golioth_lightdb_stream_set_float_async(golioth_client_t client, const char* path, float value) {
    return golioth_lightdb_set_float_internal(
            client, GOLIOTH_LIGHTDB_STREAM_PATH_PREFIX, path, value, false);
}

golioth_status_t golioth_lightdb_stream_set_string_async(
        golioth_client_t client,
        const char* path,
        const char* str,
        size_t str_len) {
    return golioth_lightdb_set_string_internal(
            client, GOLIOTH_LIGHTDB_STREAM_PATH_PREFIX, path, str, str_len, false);
}

golioth_status_t golioth_lightdb_stream_set_json_async(
        golioth_client_t client,
        const char* path,
        const char* json_str,
        size_t json_str_len) {
    return golioth_lightdb_set_json_internal(
            client, GOLIOTH_LIGHTDB_STREAM_PATH_PREFIX, path, json_str, json_str_len, false);
}

golioth_status_t
golioth_lightdb_stream_set_int_sync(golioth_client_t client, const char* path, int32_t value) {
    return golioth_lightdb_set_int_internal(
            client, GOLIOTH_LIGHTDB_STREAM_PATH_PREFIX, path, value, true);
}

golioth_status_t
golioth_lightdb_stream_set_bool_sync(golioth_client_t client, const char* path, bool value) {
    return golioth_lightdb_set_bool_internal(
            client, GOLIOTH_LIGHTDB_STREAM_PATH_PREFIX, path, value, true);
}

golioth_status_t
golioth_lightdb_stream_set_float_sync(golioth_client_t client, const char* path, float value) {
    return golioth_lightdb_set_float_internal(
            client, GOLIOTH_LIGHTDB_STREAM_PATH_PREFIX, path, value, true);
}

golioth_status_t golioth_lightdb_stream_set_string_sync(
        golioth_client_t client,
        const char* path,
        const char* str,
        size_t str_len) {
    return golioth_lightdb_set_string_internal(
            client, GOLIOTH_LIGHTDB_STREAM_PATH_PREFIX, path, str, str_len, true);
}

golioth_status_t golioth_lightdb_stream_set_json_sync(
        golioth_client_t client,
        const char* path,
        const char* json_str,
        size_t json_str_len) {
    return golioth_lightdb_set_json_internal(
            client, GOLIOTH_LIGHTDB_STREAM_PATH_PREFIX, path, json_str, json_str_len, true);
}
