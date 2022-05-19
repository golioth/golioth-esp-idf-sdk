#pragma once

#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/timers.h>
#include <coap3/coap.h> // COAP_MEDIATYPE_*
#include "golioth_client.h"
#include "golioth_lightdb.h"

typedef struct {
    // The CoAP path string (everything after coaps://coap.golioth.io/).
    // Assumption: path is a string literal (i.e. we don't need to strcpy).
    const char* path_prefix;
    const char* path;
    // Must be one of:
    //   COAP_MEDIATYPE_APPLICATION_JSON
    //   COAP_MEDIATYPE_APPLICATION_CBOR
    uint32_t content_type;
    // CoAP payload assumed to be dynamically allocated before enqueue
    // and freed after dequeue.
    uint8_t* payload;
    // Size of payload, in bytes
    size_t payload_size;
} golioth_coap_put_params_t;

typedef struct {
    const char* path_prefix;
    const char* path;
    uint32_t content_type;
    golioth_get_cb_fn callback;
    void* arg;
} golioth_coap_get_params_t;

typedef struct {
    const char* path_prefix;
    const char* path;
} golioth_coap_delete_params_t;

typedef struct {
    const char* path_prefix;
    const char* path;
    uint32_t content_type;
    golioth_get_cb_fn callback;
    void* arg;
} golioth_coap_observe_params_t;

typedef enum {
    GOLIOTH_COAP_REQUEST_GET,
    GOLIOTH_COAP_REQUEST_PUT,
    GOLIOTH_COAP_REQUEST_DELETE,
    GOLIOTH_COAP_REQUEST_OBSERVE,
} golioth_coap_request_type_t;

typedef struct {
    golioth_coap_request_type_t type;
    union {
        golioth_coap_get_params_t get;
        golioth_coap_put_params_t put;
        golioth_coap_delete_params_t delete;
        golioth_coap_observe_params_t observe;
    };
} golioth_coap_request_msg_t;

// This is the struct hidden by the opaque type golioth_client_t
// TODO - document these once design is more stable
typedef struct {
    QueueHandle_t request_queue;
    TaskHandle_t coap_task_handle;
    SemaphoreHandle_t run_sem;
    TimerHandle_t keepalive_timer;
    bool end_session;
    bool session_connected;
    uint8_t token[8];
    size_t token_len;
    bool got_coap_response;
    const char* psk_id;
    size_t psk_id_len;
    const char* psk;
    size_t psk_len;
    golioth_coap_request_msg_t pending_req;
} golioth_coap_client_t;

golioth_status_t golioth_coap_client_set_async(
        golioth_client_t client,
        const char* path_prefix,
        const char* path,
        uint32_t content_type,
        const uint8_t* payload,
        size_t payload_size);

golioth_status_t golioth_coap_client_delete_async(
        golioth_client_t client,
        const char* path_prefix,
        const char* path);

golioth_status_t golioth_coap_client_get_async(
        golioth_client_t client,
        const char* path_prefix,
        const char* path,
        uint32_t content_type,
        golioth_get_cb_fn callback,
        void* arg);

#if 0

//
// Async API
//

// Callback function signature for get and observe
typedef void (*golioth_client_get_cb_fn)(
        golioth_client_t client,
        golioth_status_t status,
        const char* path,
        const uint8_t* payload,
        size_t payload_size,
        void* arg);

// Callback function signature for set and delete
typedef void (*golioth_client_set_cb_fn)(
        golioth_client_t client,
        golioth_status_t status,
        const char* path,
        void* arg);

/// Example usage:
///
/// @code{.c}
/// void on_get(
///         golioth_client_t client,
///         golioth_status_t status,
///         const char* path,
///         const uint8_t* payload,
///         size_t payload_size,
///         void* arg) {
///     // ignore: client, path, arg
///     if (status == GOLIOTH_OK) {
///         // do something with payload, maybe memcpy
///     }
/// }
///
/// golioth_client_get(client, ".d/setting", NULL, NULL);
/// golioth_client_set(client, ".d/setting", on_get, NULL);
/// golioth_client_set(client, ".d/setting", on_get, &some_struct);
/// @endcode
golioth_status_t golioth_client_get(
        golioth_client_t client,
        const char* path,
        golioth_client_get_cb_fn callback,
        void* arg);
golioth_status_t golioth_client_observe(
        golioth_client_t client,
        const char* path,
        golioth_client_get_cb_fn callback,
        void* arg);

/// Example usage:
///
/// @code{.c}
/// void on_set(
///         golioth_client_t client,
///         golioth_status_t status,
///         const char* path,
///         void* arg) {
///     // ignore: client, path, arg
///     if (status != GOLIOTH_OK) {
///         // handle error...
///     }
/// }
///
/// uint32_t value = htonl(4);
/// golioth_client_set(client, ".d/setting", &value, sizeof(value), NULL, NULL);
/// golioth_client_set(client, ".d/setting", &value, sizeof(value), on_set, NULL);
/// golioth_client_set(client, ".d/setting", &value, sizeof(value), on_set, &some_struct);
/// @endcode
golioth_status_t golioth_client_set(
        golioth_client_t client,
        const char* path,
        const uint8_t* payload,
        size_t payload_size,
        golioth_client_set_cb_fn callback,
        void* arg);
golioth_status_t golioth_client_delete(
        golioth_client_t client,
        const char* path,
        golioth_client_set_cb_fn callback,
        void* arg);

// Sync API
golioth_status_t golioth_client_get_sync(
        golioth_client_t client,
        const char* path,
        uint8_t* payload_buf,
        size_t payload_buf_size,
        size_t* payload_size);
golioth_status_t golioth_client_set_sync(
        golioth_client_t client,
        const char* path,
        const uint8_t* payload,
        size_t payload_size);
golioth_status_t golioth_client_delete_sync(
        golioth_client_t client,
        const char* path);


/// LightDB example usage

typedef void (*golioth_lightdb_get_string_cb_fn)(
        golioth_client_t client,
        golioth_status_t status,
        const char* path,
        const char* string,
        size_t string_len,
        void* arg);

golioth_status_t golioth_lightdb_observe_string(
        golioth_client_t client,
        const char* path,
        golioth_lightdb_get_string_cb_fn callback,
        void* arg) {
    golioth_coap_client_t* c = (golioth_coap_client_t*)client;
    if (!c) {
        return GOLIOTH_ERR_NULL;
    }

    static char fullpath[32] = {};
    snprintf(fullpath, 32, ".d/%s", path);
    ESP_LOGD(TAG, "OBSERVE \"%s\"", fullpath);

    golioth_client_observe(c, fullpath,

    golioth_coap_request_msg_t request_msg = {
        .type = GOLIOTH_COAP_REQUEST_OBSERVE,
        .observe = {
            .path = fullpath,
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

/// Idea: App registers single handler for each type (get/observe/set/delete)
/// When calling get(), can pass in arg
/// Request stores the arg

typedef void (*golioth_client_get_cb_fn)(
        golioth_client_t client,
        golioth_status_t status,
        const char* path,
        const uint8_t* payload,
        size_t payload_size,
        void* arg);

typedef void (*golioth_client_set_cb_fn)(
        golioth_client_t client,
        golioth_status_t status,
        const char* path,
        void* arg);


void on_get(
        golioth_client_t client,
        golioth_status_t status,
        const char* path,
        const uint8_t* payload,
        size_t payload_size,
        void* arg) {
    if (status == GOLIOTH_OK) {
        ldb_path_id_t id = (ldb_path_id_t)arg;
        switch (id) {
            case LDB_PATH_ID_SETTING:
                // int32_t value = ntohl(*(int32_t*)payload);
                int32_t value = golioth_payload_as_int(payload, payload_size);
                break;
            default:
                break;
        }
    }
}

/// Top-down, create a nice async user API

// App-specific enum, to avoid strcmp on path in callback
typedef enum {
    LDB_PATH_ID_SETTING
} ldb_path_id_t;

typedef void (*golioth_client_get_cb_fn)(
        const char* path,
        const uint8_t* payload,
        size_t payload_size,
        void* arg);

golioth_lightdb_set_int(client, "setting", 42);
golioth_lightdb_set_bool(client, "setting", false);
golioth_lightdb_set_string(client, "setting", "value", strlen("value")); // also for json
golioth_lightdb_delete(client, "setting");
golioth_lightdb_get(client, "setting", on_get, (void*)LDB_PATH_ID_SETTING);
golioth_lightdb_observe(client, "setting", on_get, (void*)LDB_PATH_ID_SETTING);
golioth_log_info(client, "module", "message");
golioth_log_debug(client, "module", "message");
golioth_log_warn(client, "module", "message");
golioth_log_error(client, "module", "message");
golioth_register_dfu_handler(client, on_dfu, NULL);

// low-level api
golioth_coap_get(client, path_prefix, path, callback, arg);
golioth_coap_observe(client, path_prefix, path, callback, arg);
golioth_coap_set(client, path_prefix, path, payload, payload_size);
golioth_coap_delete(client, path_prefix, path);
#endif
