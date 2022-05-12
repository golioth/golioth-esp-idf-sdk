#pragma once

#include "golioth_status.h"
#include "golioth_client.h"

// TODO - user registers callback and content type
golioth_status_t golioth_lightdb_observe(golioth_client_t client, const char* path);

// TODO - user provides content type
// TODO - set_int, set_string, set_json, set_bool, set_float
golioth_status_t golioth_lightdb_set(
        golioth_client_t client,
        const char* path,
        const uint8_t* payload,
        size_t payload_size);

// TODO - user registers callback and content type
golioth_status_t golioth_lightdb_get(golioth_client_t client, const char* path);

golioth_status_t golioth_lightdb_delete(golioth_client_t client, const char* path);
