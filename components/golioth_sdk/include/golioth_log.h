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
golioth_status_t golioth_log_error(golioth_client_t client, const char* tag, const char* log_message);
golioth_status_t golioth_log_warn(golioth_client_t client, const char* tag, const char* log_message);
golioth_status_t golioth_log_info(golioth_client_t client, const char* tag, const char* log_message);
golioth_status_t golioth_log_debug(golioth_client_t client, const char* tag, const char* log_message);

// Sync APIs (blocking, wait for server response or timeout)
golioth_status_t golioth_log_error_sync(golioth_client_t client, const char* tag, const char* log_message);
golioth_status_t golioth_log_warn_sync(golioth_client_t client, const char* tag, const char* log_message);
golioth_status_t golioth_log_info_sync(golioth_client_t client, const char* tag, const char* log_message);
golioth_status_t golioth_log_debug_sync(golioth_client_t client, const char* tag, const char* log_message);
