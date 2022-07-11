/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cJSON.h>
#include "golioth_status.h"
#include "golioth_client.h"

typedef enum {
    RPC_OK = 0,
    RPC_CANCELED = 1,
    RPC_UNKNOWN = 2,
    RPC_INVALID_ARGUMENT = 3,
    RPC_DEADLINE_EXCEEDED = 4,
    RPC_NOT_FOUND = 5,
    RPC_ALREADYEXISTS = 6,
    RPC_PERMISSION_DENIED = 7,
    RPC_RESOURCE_EXHAUSTED = 8,
    RPC_FAILED_PRECONDITION = 9,
    RPC_ABORTED = 10,
    RPC_OUT_OF_RANGE = 11,
    RPC_UNIMPLEMENTED = 12,
    RPC_INTERNAL = 13,
    RPC_UNAVAILABLE = 14,
    RPC_DATA_LOSS = 15,
    RPC_UNAUTHENTICATED = 16,
} golioth_rpc_status_t;

// Callback function type for remote procedure call
typedef golioth_rpc_status_t (*golioth_rpc_cb_fn)(
        golioth_client_t client,
        const char* method,
        const cJSON* params,
        uint8_t* detail,
        size_t detail_size);

// clang-format off

golioth_status_t golioth_rpc_register(golioth_client_t client, const char* method, golioth_rpc_cb_fn callback);

//clang-format on
