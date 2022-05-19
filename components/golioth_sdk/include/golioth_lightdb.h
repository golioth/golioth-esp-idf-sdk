#pragma once

#include "golioth_status.h"
#include "golioth_client.h"

typedef void (*golioth_get_cb_fn)(
        golioth_client_t client,
        const char* path,
        const uint8_t* payload,
        size_t payload_size,
        void* arg);

// Deserialization
// TODO - find a better home for these?
int32_t golioth_payload_as_int(const uint8_t* payload, size_t payload_size);
float golioth_payload_as_float(const uint8_t* payload, size_t payload_size);
bool golioth_payload_is_null(const uint8_t* payload, size_t payload_size);

// Async API
golioth_status_t golioth_lightdb_set_int(
        golioth_client_t client,
        const char* path,
        int32_t value);
golioth_status_t golioth_lightdb_set_bool(
        golioth_client_t client,
        const char* path,
        bool value);
golioth_status_t golioth_lightdb_set_float(
        golioth_client_t client,
        const char* path,
        float value);
golioth_status_t golioth_lightdb_set_string(
        golioth_client_t client,
        const char* path,
        const char* str,
        size_t str_len);
golioth_status_t golioth_lightdb_delete(
        golioth_client_t client,
        const char* path);
golioth_status_t golioth_lightdb_get(
        golioth_client_t client,
        const char* path,
        golioth_get_cb_fn callback,
        void* arg);
golioth_status_t golioth_lightdb_observe(
        golioth_client_t client,
        const char* path,
        golioth_get_cb_fn callback,
        void* arg);
