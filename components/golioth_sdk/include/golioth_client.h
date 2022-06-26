/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stdlib.h>
#include "golioth_status.h"

// Opaque handle to the Golioth client
typedef void* golioth_client_t;

typedef enum {
    GOLIOTH_CLIENT_EVENT_CONNECTED,
    GOLIOTH_CLIENT_EVENT_DISCONNECTED,
} golioth_client_event_t;

// For Golioth APIs that take a timeout parameter
#define GOLIOTH_WAIT_FOREVER -1

typedef struct {
    // One of:
    //      GOLIOTH_ERR_TIMEOUT (no response received from server)
    //      GOLIOTH_OK (2.XX)
    //      GOLIOTH_ERR_FAIL (anything other than 2.XX)
    golioth_status_t status;
    uint8_t class;  // the 2 in 2.XX
    uint8_t code;   // the 03 in 4.03
} golioth_response_t;

// Callback function type for client events
typedef void (*golioth_client_event_cb_fn)(
        golioth_client_t client,
        golioth_client_event_t event,
        void* arg);

// Callback function type for all asynchronous get/observe requests
typedef void (*golioth_get_cb_fn)(
        golioth_client_t client,
        const golioth_response_t* response,
        const char* path,
        const uint8_t* payload,
        size_t payload_size,
        void* arg);

// Callback function type for all asynchronous set/delete requests
typedef void (*golioth_set_cb_fn)(
        golioth_client_t client,
        const golioth_response_t* response,
        const char* path,
        void* arg);

golioth_client_t golioth_client_create(const char* psk_id, const char* psk);
// Note: client automatically started by golioth_client_create
golioth_status_t golioth_client_start(golioth_client_t client);
golioth_status_t golioth_client_stop(golioth_client_t client);
golioth_status_t golioth_client_is_running(golioth_client_t client);
void golioth_client_destroy(golioth_client_t client);
bool golioth_client_is_connected(golioth_client_t client);

// Register a callback that will be called on client events (e.g. connected, disconnected)
void golioth_client_register_event_callback(
        golioth_client_t client,
        golioth_client_event_cb_fn callback,
        void* arg);

uint32_t golioth_client_task_stack_min_remaining(golioth_client_t client);
