/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "golioth_status.h"
#include "golioth_client.h"

typedef enum {
    GOLIOTH_LOG_LEVEL_ERROR,
    GOLIOTH_LOG_LEVEL_WARN,
    GOLIOTH_LOG_LEVEL_INFO,
    GOLIOTH_LOG_LEVEL_DEBUG
} golioth_log_level_t;

// Async APIs (non-blocking)
golioth_status_t golioth_log_error_async(
        golioth_client_t client,
        const char* tag,
        const char* log_message,
        golioth_set_cb_fn callback,
        void* callback_arg);
golioth_status_t golioth_log_warn_async(
        golioth_client_t client,
        const char* tag,
        const char* log_message,
        golioth_set_cb_fn callback,
        void* callback_arg);
golioth_status_t golioth_log_info_async(
        golioth_client_t client,
        const char* tag,
        const char* log_message,
        golioth_set_cb_fn callback,
        void* callback_arg);
golioth_status_t golioth_log_debug_async(
        golioth_client_t client,
        const char* tag,
        const char* log_message,
        golioth_set_cb_fn callback,
        void* callback_arg);

// Sync APIs (blocking, wait for server response or timeout)
golioth_status_t golioth_log_error_sync(
        golioth_client_t client,
        const char* tag,
        const char* log_message,
        int32_t timeout_s);
golioth_status_t golioth_log_warn_sync(
        golioth_client_t client,
        const char* tag,
        const char* log_message,
        int32_t timeout_s);
golioth_status_t golioth_log_info_sync(
        golioth_client_t client,
        const char* tag,
        const char* log_message,
        int32_t timeout_s);
golioth_status_t golioth_log_debug_sync(
        golioth_client_t client,
        const char* tag,
        const char* log_message,
        int32_t timeout_s);
