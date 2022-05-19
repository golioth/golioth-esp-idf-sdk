#pragma once

#include "golioth_status.h"
#include "golioth_client.h"

typedef enum {
    GOLIOTH_LOG_LEVEL_ERROR,
    GOLIOTH_LOG_LEVEL_WARN,
    GOLIOTH_LOG_LEVEL_INFO,
    GOLIOTH_LOG_LEVEL_DEBUG
} golioth_log_level_t;

golioth_status_t golioth_log_error(golioth_client_t client, const char* tag, const char* log_message);
golioth_status_t golioth_log_warn(golioth_client_t client, const char* tag, const char* log_message);
golioth_status_t golioth_log_info(golioth_client_t client, const char* tag, const char* log_message);
golioth_status_t golioth_log_debug(golioth_client_t client, const char* tag, const char* log_message);
golioth_status_t golioth_log(golioth_client_t client, golioth_log_level_t level, const char* tag, const char* log_message);
