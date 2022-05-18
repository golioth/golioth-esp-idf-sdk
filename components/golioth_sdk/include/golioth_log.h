#pragma once

#include "golioth_status.h"
#include "golioth_client.h"

golioth_status_t golioth_log_error(golioth_client_t client, const char* tag, const char* log_message);
golioth_status_t golioth_log_warn(golioth_client_t client, const char* tag, const char* log_message);
golioth_status_t golioth_log_info(golioth_client_t client, const char* tag, const char* log_message);
golioth_status_t golioth_log_debug(golioth_client_t client, const char* tag, const char* log_message);
