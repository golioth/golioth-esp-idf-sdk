/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>
#include "golioth_status.h"
#include "golioth_client.h"

#define GOLIOTH_OTA_BLOCKSIZE 1024

typedef enum {
    GOLIOTH_OTA_STATE_IDLE,
    GOLIOTH_OTA_STATE_DOWNLOADING,
    GOLIOTH_OTA_STATE_DOWNLOADED,
    GOLIOTH_OTA_STATE_UPDATING,
} golioth_ota_state_t;

typedef enum {
    GOLIOTH_OTA_REASON_READY,
    GOLIOTH_OTA_REASON_FIRMWARE_UPDATED_SUCCESSFULLY,
    GOLIOTH_OTA_REASON_NOT_ENOUGH_FLASH_MEMORY,
    GOLIOTH_OTA_REASON_OUT_OF_RAM,
    GOLIOTH_OTA_REASON_CONNECTION_LOST,
    GOLIOTH_OTA_REASON_INTEGRITY_CHECK_FAILURE,
    GOLIOTH_OTA_REASON_UNSUPPORTED_PACKAGE_TYPE,
    GOLIOTH_OTA_REASON_INVALID_URI,
    GOLIOTH_OTA_REASON_FIRMWARE_UPDATE_FAILED,
    GOLIOTH_OTA_REASON_UNSUPPORTED_PROTOCOL,
} golioth_ota_reason_t;

typedef struct {
    char package[CONFIG_GOLIOTH_OTA_MAX_PACKAGE_NAME_LEN + 1];
    char version[CONFIG_GOLIOTH_OTA_MAX_VERSION_LEN + 1];
    int32_t size;
} golioth_ota_component_t;

typedef struct {
    int32_t seqnum;
    golioth_ota_component_t components[CONFIG_GOLIOTH_OTA_MAX_NUM_COMPONENTS];
    size_t num_components;
} golioth_ota_manifest_t;

golioth_status_t golioth_ota_payload_as_manifest(
        const uint8_t* payload,
        size_t payload_len,
        golioth_ota_manifest_t* manifest);

size_t golioth_ota_size_to_nblocks(size_t component_size);
const golioth_ota_component_t*
golioth_ota_find_component(const golioth_ota_manifest_t* manifest, const char* package);

// Asynchronous
golioth_status_t
golioth_ota_observe_manifest_async(golioth_client_t client, golioth_get_cb_fn callback, void* arg);

// Synchronous (wait for server response)
golioth_status_t golioth_ota_get_block_sync(
        golioth_client_t client,
        const char* package,
        const char* version,
        size_t block_index,
        uint8_t* buf,  // must be at least GOLIOTH_OTA_BLOCKSIZE bytes
        size_t* block_nbytes);
golioth_status_t golioth_ota_report_state_sync(
        golioth_client_t client,
        golioth_ota_state_t state,
        golioth_ota_reason_t reason,
        const char* package,
        const char* current_version,  // optional, can be NULL
        const char* target_version);  // optional, can be NULL
