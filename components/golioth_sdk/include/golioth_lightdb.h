/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "golioth_status.h"
#include "golioth_client.h"

// Deserialization
int32_t golioth_payload_as_int(const uint8_t* payload, size_t payload_size);
float golioth_payload_as_float(const uint8_t* payload, size_t payload_size);
bool golioth_payload_as_bool(const uint8_t* payload, size_t payload_size);
bool golioth_payload_is_null(const uint8_t* payload, size_t payload_size);

// TODO - block transfers for large post/get

//
// LightDB State
//

// set
golioth_status_t golioth_lightdb_set_int_async(
        golioth_client_t client,
        const char* path,
        int32_t value,
        golioth_set_cb_fn callback,
        void* callback_arg);
golioth_status_t golioth_lightdb_set_int_sync(
        golioth_client_t client,
        const char* path,
        int32_t value,
        int32_t timeout_s);
golioth_status_t golioth_lightdb_set_bool_async(
        golioth_client_t client,
        const char* path,
        bool value,
        golioth_set_cb_fn callback,
        void* callback_arg);
golioth_status_t golioth_lightdb_set_bool_sync(
        golioth_client_t client,
        const char* path,
        bool value,
        int32_t timeout_s);
golioth_status_t golioth_lightdb_set_float_async(
        golioth_client_t client,
        const char* path,
        float value,
        golioth_set_cb_fn callback,
        void* callback_arg);
golioth_status_t golioth_lightdb_set_float_sync(
        golioth_client_t client,
        const char* path,
        float value,
        int32_t timeout_s);
golioth_status_t golioth_lightdb_set_string_async(
        golioth_client_t client,
        const char* path,
        const char* str,
        size_t str_len,
        golioth_set_cb_fn callback,
        void* callback_arg);
golioth_status_t golioth_lightdb_set_string_sync(
        golioth_client_t client,
        const char* path,
        const char* str,
        size_t str_len,
        int32_t timeout_s);
golioth_status_t golioth_lightdb_set_json_async(
        golioth_client_t client,
        const char* path,
        const char* json_str,
        size_t json_str_len,
        golioth_set_cb_fn callback,
        void* callback_arg);
golioth_status_t golioth_lightdb_set_json_sync(
        golioth_client_t client,
        const char* path,
        const char* json_str,
        size_t json_str_len,
        int32_t timeout_s);

// get
golioth_status_t golioth_lightdb_get_async(
        golioth_client_t client,
        const char* path,
        golioth_get_cb_fn callback,
        void* arg);
golioth_status_t golioth_lightdb_get_int_sync(
        golioth_client_t client,
        const char* path,
        int32_t* value,
        int32_t timeout_s);
golioth_status_t golioth_lightdb_get_bool_sync(
        golioth_client_t client,
        const char* path,
        bool* value,
        int32_t timeout_s);
golioth_status_t golioth_lightdb_get_float_sync(
        golioth_client_t client,
        const char* path,
        float* value,
        int32_t timeout_s);
golioth_status_t golioth_lightdb_get_string_sync(
        golioth_client_t client,
        const char* path,
        char* strbuf,
        size_t strbuf_size,
        int32_t timeout_s);
golioth_status_t golioth_lightdb_get_json_sync(
        golioth_client_t client,
        const char* path,
        char* strbuf,
        size_t strbuf_size,
        int32_t timeout_s);

// delete
golioth_status_t golioth_lightdb_delete_async(
        golioth_client_t client,
        const char* path,
        golioth_set_cb_fn callback,
        void* callback_arg);
golioth_status_t golioth_lightdb_delete_sync(
        golioth_client_t client,
        const char* path,
        int32_t timeout_s);

// observe
golioth_status_t golioth_lightdb_observe_async(
        golioth_client_t client,
        const char* path,
        golioth_get_cb_fn callback,
        void* arg);

//
// LightDB Stream
//

golioth_status_t golioth_lightdb_stream_set_int_async(
        golioth_client_t client,
        const char* path,
        int32_t value,
        golioth_set_cb_fn callback,
        void* callback_arg);
golioth_status_t golioth_lightdb_stream_set_int_sync(
        golioth_client_t client,
        const char* path,
        int32_t value,
        int32_t timeout_s);
golioth_status_t golioth_lightdb_stream_set_bool_async(
        golioth_client_t client,
        const char* path,
        bool value,
        golioth_set_cb_fn callback,
        void* callback_arg);
golioth_status_t golioth_lightdb_stream_set_bool_sync(
        golioth_client_t client,
        const char* path,
        bool value,
        int32_t timeout_s);
golioth_status_t golioth_lightdb_stream_set_float_async(
        golioth_client_t client,
        const char* path,
        float value,
        golioth_set_cb_fn callback,
        void* callback_arg);
golioth_status_t golioth_lightdb_stream_set_float_sync(
        golioth_client_t client,
        const char* path,
        float value,
        int32_t timeout_s);
golioth_status_t golioth_lightdb_stream_set_string_async(
        golioth_client_t client,
        const char* path,
        const char* str,
        size_t str_len,
        golioth_set_cb_fn callback,
        void* callback_arg);
golioth_status_t golioth_lightdb_stream_set_string_sync(
        golioth_client_t client,
        const char* path,
        const char* str,
        size_t str_len,
        int32_t timeout_s);
golioth_status_t golioth_lightdb_stream_set_json_async(
        golioth_client_t client,
        const char* path,
        const char* json_str,
        size_t json_str_len,
        golioth_set_cb_fn callback,
        void* callback_arg);
golioth_status_t golioth_lightdb_stream_set_json_sync(
        golioth_client_t client,
        const char* path,
        const char* json_str,
        size_t json_str_len,
        int32_t timeout_s);
