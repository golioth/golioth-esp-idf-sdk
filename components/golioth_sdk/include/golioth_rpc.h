/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cJSON.h>
#include "golioth_status.h"
#include "golioth_client.h"

// TOOD: create RPC enum 

#define RPC_OK 0
#define RPC_CANCELED 1
#define RPC_UNKNOWN 2
#define RPC_INVALID_ARGUMENT 3
#define RPC_DEADLINE_EXCEEDED 4
#define RPC_NOT_FOUND 5
#define RPC_ALREADYEXISTS 6
#define RPC_PERMISSION_DENIED 7
#define RPC_RESOURCE_EXHAUSTED 8
#define RPC_FAILED_PRECONDITION 9
#define RPC_ABORTED 10
#define RPC_OUT_OF_RANGE 11
#define RPC_UNIMPLEMENTED 12
#define RPC_INTERNAL 13
#define RPC_UNAVAILABLE 14
#define RPC_DATA_LOSS 15
#define RPC_UNAUTHENTICATED 16

// Callback function type for remote procedure call 
typedef uint8_t (*golioth_rpc_cb_fn)(
        golioth_client_t client,
        const char* method,
        const cJSON* params,
        uint8_t *detail,
        size_t detail_size);

// clang-format off

golioth_status_t golioth_rpc_register(golioth_client_t client, const char* method, golioth_rpc_cb_fn callback);

//clang-format on
