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
    uint32_t content_type;
    size_t block_index;
    size_t block_size;
    golioth_get_cb_fn callback;
    void* arg;
} golioth_coap_get_block_params_t;

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
    GOLIOTH_COAP_REQUEST_EMPTY,
    GOLIOTH_COAP_REQUEST_GET,
    GOLIOTH_COAP_REQUEST_GET_BLOCK,
    GOLIOTH_COAP_REQUEST_PUT,
    // TODO - support for PUT_BLOCK
    GOLIOTH_COAP_REQUEST_DELETE,
    GOLIOTH_COAP_REQUEST_OBSERVE,
} golioth_coap_request_type_t;

typedef struct {
    golioth_coap_request_type_t type;
    union {
        golioth_coap_get_params_t get;
        golioth_coap_get_block_params_t get_block;
        golioth_coap_put_params_t put;
        golioth_coap_delete_params_t delete;
        golioth_coap_observe_params_t observe;
    };
    SemaphoreHandle_t request_complete_sem;
} golioth_coap_request_msg_t;

typedef struct {
    bool in_use;
    golioth_coap_observe_params_t req_params;
    uint8_t token[8];
    size_t token_len;
} golioth_coap_observe_info_t;

golioth_status_t golioth_coap_client_empty(golioth_client_t client, bool is_synchronous);

golioth_status_t golioth_coap_client_set(
        golioth_client_t client,
        const char* path_prefix,
        const char* path,
        uint32_t content_type,
        const uint8_t* payload,
        size_t payload_size,
        bool is_synchronous);

golioth_status_t golioth_coap_client_delete(
        golioth_client_t client,
        const char* path_prefix,
        const char* path,
        bool is_synchronous);

golioth_status_t golioth_coap_client_get(
        golioth_client_t client,
        const char* path_prefix,
        const char* path,
        uint32_t content_type,
        golioth_get_cb_fn callback,
        void* arg,
        bool is_synchronous);

golioth_status_t golioth_coap_client_get_block(
        golioth_client_t client,
        const char* path_prefix,
        const char* path,
        uint32_t content_type,
        size_t block_index,
        size_t block_size,
        golioth_get_cb_fn callback,
        void* arg,
        bool is_synchronous);

golioth_status_t golioth_coap_client_observe_async(
        golioth_client_t client,
        const char* path_prefix,
        const char* path,
        uint32_t content_type,
        golioth_get_cb_fn callback,
        void* arg);
